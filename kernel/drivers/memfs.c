#include <drivers/memfs.h>
#include <asm/mm.h>
#include <string.h>

static char* dup_name(const char *s) {
    char *n = valloc(256);
    strncpy(n, s, 255);
    n[255] = '\0';
    return n;
}

static memfs_dentry* dentry_create(memfs_inode *inode, const char *name) {
    memfs_dentry *d = valloc(sizeof(memfs_dentry));
    if (!d) return NULL;

    d->name = dup_name(name);
    d->inode = inode;
    d->next = NULL;
    return d;
}

static const char *path_skip(const char *p) {
    while (*p == '/') p++;
    return p;
}

static size_t path_comp_len(const char *p) {
    size_t len = 0;
    while (p[len] && p[len] != '/') len++;
    return len;
}

static int dir_add(memfs_inode *dir, memfs_inode *child) {
    if (!dir || dir->type != MEMFS_TYPE_DIR || !child) return -1;

    memfs_dentry *d = dentry_create(child, child->name);
    if (!d) return -1;

    d->next = dir->dir.entries;
    dir->dir.entries = d;
    dir->dir.entry_count++;

    child->link_count++;
    return 0;
}

static memfs_inode* lookup_local(memfs_inode *dir, const char *name) {
    if (!dir || dir->type != MEMFS_TYPE_DIR) return NULL;

    memfs_dentry *d = dir->dir.entries;
    while (d) {
        if (strcmp(d->name, name) == 0) return d->inode;
        d = d->next;
    }
    return NULL;
}

memfs_inode* lookup_path(memfs *fs, const char *path) {
    if (!fs || !path) return NULL;
    
    if (strcmp(path, "/") == 0) return fs->root;
    
    if (path[0] != '/') return NULL;
    
    memfs_inode *cur = fs->root;
    path = path_skip(path);
    
    while (*path) {
        if (!cur || cur->type != MEMFS_TYPE_DIR) return NULL;
        
        size_t len = path_comp_len(path);
        char name[256];
        memcpy(name, path, len);
        name[len] = '\0';
        
        memfs_inode *next = lookup_local(cur, name);
        if (!next) return NULL;
        
        cur = next;
        path += len;
        path = path_skip(path);
    }
    return cur;
}

static memfs_inode* mkdir_p(memfs *fs, const char *path) {
    memfs_inode *cur = fs->root;
    path = path_skip(path);

    while (*path) {
        size_t len = path_comp_len(path);
        char name[256];
        memcpy(name, path, len);
        name[len] = '\0';

        memfs_inode *next = lookup_local(cur, name);
        if (!next) {
            next = valloc(sizeof(memfs_inode));
            memset(next, 0, sizeof(memfs_inode));

            next->type = MEMFS_TYPE_DIR;
            next->name = dup_name(name);
            next->dir.parent = cur;

            dir_add(cur, next);
            fs->inode_count++;
        }

        if (next->type != MEMFS_TYPE_DIR)
            return NULL;

        cur = next;
        path += len;
        path = path_skip(path);
    }
    return cur;
}

static memfs_inode* split_parent(memfs *fs, const char *path, char *out_name) {
    const char *last = strrchr(path, '/');
    size_t namelen = strlen(last + 1);
    memcpy(out_name, last + 1, namelen);
    out_name[namelen] = '\0';

    char dirpath[256];
    size_t dirlen = last - path;
    memcpy(dirpath, path, dirlen);
    dirpath[dirlen] = '\0';
    if (dirlen == 0) strcpy(dirpath, "/");

    return mkdir_p(fs, dirpath);
}

memfs* memfs_create(size_t size) {
    memfs *fs = valloc(sizeof(memfs));
    if (!fs) return NULL;
    
    memfs_inode *root = valloc(sizeof(memfs_inode));
    if (!root) {
        vfree(fs);
        return NULL;
    }
    
    memset(fs, 0, sizeof(memfs));
    memset(root, 0, sizeof(memfs_inode));
    
    root->type = MEMFS_TYPE_DIR;
    root->name = dup_name("/");
    if (!root->name) {
        vfree(root);
        vfree(fs);
        return NULL;
    }
    
    root->dir.parent = NULL;
    root->dir.entries = NULL;
    root->dir.entry_count = 0;
    root->link_count = 1;
    
    fs->root = root;
    fs->inode_count = 1;
    fs->total_memory = size;
    fs->used_memory = 0;
    
    return fs;
}

memfs_inode* memfs_create_dir(memfs *fs, const char *path) {
    if (!fs || !path || path[0] != '/') return NULL;

    char name[256];
    memfs_inode *parent = split_parent(fs, path, name);
    if (!parent) return NULL;

    memfs_inode *dir = valloc(sizeof(memfs_inode));
    memset(dir, 0, sizeof(memfs_inode));

    dir->type = MEMFS_TYPE_DIR;
    dir->name = dup_name(name);
    dir->dir.parent = parent;

    dir_add(parent, dir);
    fs->inode_count++;

    return dir;
}

memfs_inode* memfs_create_file(memfs *fs, const char *path) {
    if (!fs || !path || path[0] != '/') return NULL;

    char name[256];
    memfs_inode *parent = split_parent(fs, path, name);
    if (!parent) return NULL;

    memfs_inode *file = valloc(sizeof(memfs_inode));
    memset(file, 0, sizeof(memfs_inode));

    file->type = MEMFS_TYPE_FILE;
    file->name = dup_name(name);
    file->file.size = 0;
    file->file.data = NULL;

    dir_add(parent, file);
    fs->inode_count++;

    return file;
}

int memfs_delete_file(memfs *fs, const char *path) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_FILE) return -1;

    if (node->file.data) vfree(node->file.data);

    node->link_count--;
    return 0;
}

int memfs_delete_dir(memfs *fs, const char *path) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_DIR) return -1;

    if (node->dir.entry_count > 0) return -1;

    node->link_count--;
    return 0;
}

int memfs_open(memfs *fs, const char *path) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node) return -1;
    return (int)(uintptr_t)node;
}

int memfs_close(int fd) {
    (void)fd;
    return 0;
}

ssize_t memfs_write(memfs *fs, const char *path, const void *buf, size_t size) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_FILE) return -1;

    if (node->file.data) vfree(node->file.data);

    node->file.data = valloc(size);
    if (!node->file.data) return -1;

    memcpy(node->file.data, buf, size);
    node->file.size = size;
    fs->used_memory += size;

    return size;
}

ssize_t memfs_read(memfs *fs, const char *path, void *buf, size_t size) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_FILE) return -1;

    size_t to_copy = (size < node->file.size) ? size : node->file.size;
    memcpy(buf, node->file.data, to_copy);
    return to_copy;
}

int memfs_get_info(memfs *fs, const char *path, memfs_inode *out) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node) return -1;

    memcpy(out, node, sizeof(memfs_inode));
    return 0;
}

memfs_inode* memfs_search(memfs *fs, const char *name) {
    if (!fs) return NULL;

    memfs_inode *stack[128];
    int sp = 0;
    stack[sp++] = fs->root;

    while (sp) {
        memfs_inode *cur = stack[--sp];

        if (strcmp(cur->name, name) == 0) return cur;

        if (cur->type == MEMFS_TYPE_DIR) {
            memfs_dentry *d = cur->dir.entries;
            while (d) {
                stack[sp++] = d->inode;
                d = d->next;
            }
        }
    }
    return NULL;
}

int memfs_link(memfs *fs, const char *oldpath, const char *newpath) {
    memfs_inode *old = lookup_path(fs, oldpath);
    if (!old) return -1;

    char name[256];
    memfs_inode *parent = split_parent(fs, newpath, name);
    if (!parent) return -1;

    dir_add(parent, old);
    return 0;
}

ssize_t memfs_append(memfs *fs, const char *path, const void *buf, size_t size) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_FILE) return -1;

    uint8_t *newbuf = valloc(node->file.size + size);
    if (!newbuf) return -1;

    if (node->file.data) {
        memcpy(newbuf, node->file.data, node->file.size);
        vfree(node->file.data);
    }

    memcpy(newbuf + node->file.size, buf, size);

    fs->used_memory += size;
    node->file.size += size;
    node->file.data = newbuf;

    return node->file.size;
}

size_t memfs_readdir(memfs *fs, const char *path, char **names, size_t max_entries) {
    memfs_inode *dir = lookup_path(fs, path);
    if (!dir || dir->type != MEMFS_TYPE_DIR) return 0;

    size_t count = 0;
    memfs_dentry *d = dir->dir.entries;

    while (d && count < max_entries) {
        names[count] = d->name;
        count++;
        d = d->next;
    }

    return count;
}

char* memfs_ls(memfs *fs, const char *path) {
    memfs_inode *dir = lookup_path(fs, path);
    if (!dir || dir->type != MEMFS_TYPE_DIR) return NULL;

    char *out = valloc(dir->dir.entry_count * 256 + 1);
    if (!out) return NULL;

    out[0] = '\0';

    memfs_dentry *d = dir->dir.entries;
    while (d) {
        strcat(out, d->name);
        if (d->next)
            strcat(out, "\n");
        d = d->next;
    }

    return out;
}