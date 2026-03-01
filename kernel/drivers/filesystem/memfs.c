#include <drivers/filesystem/memfs.h>
#include <asm/mm.h>
#include <string.h>

static char* dup_name(const char *s) {
    char *n = valloc(256);
    if (!n) return NULL;
    strncpy(n, s, 255);
    n[255] = '\0';
    return n;
}

static memfs_dentry* dentry_create(memfs_inode *inode, const char *name) {
    memfs_dentry *d = valloc(sizeof(memfs_dentry));
    if (!d) return NULL;
    d->name = dup_name(name);
    d->inode = inode;
    d->mounted_fs = NULL;
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
    memfs_dentry *d;
    if (!dir || dir->type != MEMFS_TYPE_DIR || !child) return -1;
    d = dentry_create(child, child->name);
    if (!d) return -1;
    d->next = dir->dir.entries;
    dir->dir.entries = d;
    dir->dir.entry_count++;
    child->link_count++;
    return 0;
}

static memfs_dentry* lookup_dentry(memfs_inode *dir, const char *name) {
    memfs_dentry *d;
    if (!dir || dir->type != MEMFS_TYPE_DIR) return NULL;
    d = dir->dir.entries;
    while (d) {
        if (strcmp(d->name, name) == 0) return d;
        d = d->next;
    }
    return NULL;
}

static memfs_dentry* lookup_path_dentry(memfs *fs, const char *path, memfs **owner_fs, memfs_inode **owner_parent) {
    memfs *cur_fs;
    memfs_inode *cur;
    if (!fs || !path || path[0] != '/') return NULL;
    if (strcmp(path, "/") == 0) return NULL;

    cur_fs = fs;
    cur = fs->root;
    path = path_skip(path);
    while (*path) {
        size_t len;
        char name[256];
        memfs_dentry *d;
        const char *next_path;
        if (!cur || cur->type != MEMFS_TYPE_DIR) return NULL;
        len = path_comp_len(path);
        memcpy(name, path, len);
        name[len] = '\0';
        d = lookup_dentry(cur, name);
        if (!d) return NULL;
        next_path = path_skip(path + len);
        if (*next_path == '\0') {
            if (owner_fs) *owner_fs = cur_fs;
            if (owner_parent) *owner_parent = cur;
            return d;
        }
        cur = d->inode;
        if (d->mounted_fs) {
            cur_fs = d->mounted_fs;
            cur = cur_fs->root;
        }
        path = next_path;
    }
    return NULL;
}

static memfs_inode* lookup_path_ex(memfs *fs, const char *path, memfs **owner_fs) {
    memfs *cur_fs;
    memfs_inode *cur;
    if (!fs || !path) return NULL;
    if (strcmp(path, "/") == 0) {
        if (owner_fs) *owner_fs = fs;
        return fs->root;
    }
    if (path[0] != '/') return NULL;

    cur_fs = fs;
    cur = fs->root;
    path = path_skip(path);
    while (*path) {
        size_t len;
        char name[256];
        memfs_dentry *d;
        memfs_inode *next;
        if (!cur || cur->type != MEMFS_TYPE_DIR) return NULL;
        len = path_comp_len(path);
        memcpy(name, path, len);
        name[len] = '\0';
        d = lookup_dentry(cur, name);
        if (!d) return NULL;
        next = d->inode;
        if (d->mounted_fs) {
            cur_fs = d->mounted_fs;
            next = cur_fs->root;
        }
        cur = next;
        path = path_skip(path + len);
    }
    if (owner_fs) *owner_fs = cur_fs;
    return cur;
}

memfs_inode* lookup_path(memfs *fs, const char *path) {
    return lookup_path_ex(fs, path, NULL);
}

static memfs_inode* mkdir_p(memfs *fs, const char *path, memfs **owner_fs) {
    memfs *cur_fs = fs;
    memfs_inode *cur = fs->root;
    path = path_skip(path);
    while (*path) {
        size_t len = path_comp_len(path);
        char name[256];
        memfs_dentry *d;
        memfs_inode *next = NULL;
        memcpy(name, path, len);
        name[len] = '\0';
        d = lookup_dentry(cur, name);
        if (!d) {
            next = valloc(sizeof(memfs_inode));
            if (!next) return NULL;
            memset(next, 0, sizeof(memfs_inode));
            next->type = MEMFS_TYPE_DIR;
            next->name = dup_name(name);
            if (!next->name) return NULL;
            next->dir.parent = cur;
            dir_add(cur, next);
            cur_fs->inode_count++;
        } else if (d->mounted_fs) {
            cur_fs = d->mounted_fs;
            next = cur_fs->root;
        } else {
            next = d->inode;
        }
        if (next->type != MEMFS_TYPE_DIR) return NULL;
        cur = next;
        path = path_skip(path + len);
    }
    if (owner_fs) *owner_fs = cur_fs;
    return cur;
}

static memfs_inode* split_parent(memfs *fs, const char *path, char *out_name, memfs **owner_fs) {
    const char *last = strrchr(path, '/');
    char dirpath[256];
    size_t namelen;
    size_t dirlen;
    if (!last) return NULL;
    namelen = strlen(last + 1);
    memcpy(out_name, last + 1, namelen);
    out_name[namelen] = '\0';
    dirlen = (size_t)(last - path);
    memcpy(dirpath, path, dirlen);
    dirpath[dirlen] = '\0';
    if (dirlen == 0) strcpy(dirpath, "/");
    return mkdir_p(fs, dirpath, owner_fs);
}

static int is_storage_node(memfs_node_type t) {
    return (t == MEMFS_TYPE_FILE || t == MEMFS_TYPE_FIFO || t == MEMFS_TYPE_SOCKET);
}

static int is_stream_node(memfs_node_type t) {
    return (t == MEMFS_TYPE_FIFO || t == MEMFS_TYPE_SOCKET);
}

static void used_sub(memfs *fs, size_t n) {
    if (!fs) return;
    if (fs->used_memory >= n) fs->used_memory -= n;
    else fs->used_memory = 0;
}

static void used_add(memfs *fs, size_t n) {
    if (!fs) return;
    fs->used_memory += n;
}

static char type_suffix(memfs_node_type t) {
    switch (t) {
        case MEMFS_TYPE_DIR: return '/';
        case MEMFS_TYPE_SYMLINK: return '@';
        case MEMFS_TYPE_DEVICE: return '*';
        case MEMFS_TYPE_FIFO: return '|';
        case MEMFS_TYPE_SOCKET: return '=';
        case MEMFS_TYPE_CHARDEV: return 'c';
        case MEMFS_TYPE_BLOCKDEV: return 'b';
        default: return '\0';
    }
}

static ssize_t stream_write(memfs *owner_fs, memfs_inode *node, const void *buf, size_t size) {
    uint8_t *newbuf;
    if (!node || !buf) return -1;
    if (size == 0) return 0;
    newbuf = valloc(node->file.size + size);
    if (!newbuf) return -1;
    if (node->file.data && node->file.size) {
        memcpy(newbuf, node->file.data, node->file.size);
        vfree(node->file.data);
    }
    memcpy(newbuf + node->file.size, buf, size);
    node->file.data = newbuf;
    node->file.size += size;
    used_add(owner_fs, size);
    return (ssize_t)size;
}

static ssize_t stream_read(memfs *owner_fs, memfs_inode *node, void *buf, size_t size) {
    size_t to_copy;
    size_t remain;
    uint8_t *newbuf = NULL;
    if (!node || !buf) return -1;
    if (size == 0) return 0;
    if (!node->file.data || node->file.size == 0) return 0;
    to_copy = (size < node->file.size) ? size : node->file.size;
    memcpy(buf, node->file.data, to_copy);
    remain = node->file.size - to_copy;
    if (remain > 0) {
        newbuf = valloc(remain);
        if (!newbuf) return -1;
        memcpy(newbuf, node->file.data + to_copy, remain);
    }
    vfree(node->file.data);
    node->file.data = newbuf;
    node->file.size = remain;
    used_sub(owner_fs, to_copy);
    return (ssize_t)to_copy;
}

memfs* memfs_create(size_t size) {
    memfs *fs = valloc(sizeof(memfs));
    memfs_inode *root;
    if (!fs) return NULL;
    root = valloc(sizeof(memfs_inode));
    if (!root) return NULL;
    memset(fs, 0, sizeof(memfs));
    memset(root, 0, sizeof(memfs_inode));
    root->type = MEMFS_TYPE_DIR;
    root->name = dup_name("/");
    if (!root->name) return NULL;
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
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *dir;
    if (!fs || !path || path[0] != '/') return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return existing->mounted_fs->root;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_DIR) return existing->inode;
        return NULL;
    }
    dir = valloc(sizeof(memfs_inode));
    if (!dir) return NULL;
    memset(dir, 0, sizeof(memfs_inode));
    dir->type = MEMFS_TYPE_DIR;
    dir->name = dup_name(name);
    dir->dir.parent = parent;
    dir_add(parent, dir);
    owner_fs->inode_count++;
    return dir;
}

memfs_inode* memfs_create_file(memfs *fs, const char *path) {
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *file;
    if (!fs || !path || path[0] != '/') return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return NULL;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_FILE) return existing->inode;
        return NULL;
    }
    file = valloc(sizeof(memfs_inode));
    if (!file) return NULL;
    memset(file, 0, sizeof(memfs_inode));
    file->type = MEMFS_TYPE_FILE;
    file->name = dup_name(name);
    file->file.size = 0;
    file->file.data = NULL;
    dir_add(parent, file);
    owner_fs->inode_count++;
    return file;
}

memfs_inode* memfs_create_fifo(memfs *fs, const char *path) {
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *node;
    if (!fs || !path || path[0] != '/') return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return NULL;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_FIFO) return existing->inode;
        return NULL;
    }
    node = valloc(sizeof(memfs_inode));
    if (!node) return NULL;
    memset(node, 0, sizeof(memfs_inode));
    node->type = MEMFS_TYPE_FIFO;
    node->name = dup_name(name);
    node->file.size = 0;
    node->file.data = NULL;
    dir_add(parent, node);
    owner_fs->inode_count++;
    return node;
}

memfs_inode* memfs_create_socket(memfs *fs, const char *path) {
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *node;
    if (!fs || !path || path[0] != '/') return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return NULL;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_SOCKET) return existing->inode;
        return NULL;
    }
    node = valloc(sizeof(memfs_inode));
    if (!node) return NULL;
    memset(node, 0, sizeof(memfs_inode));
    node->type = MEMFS_TYPE_SOCKET;
    node->name = dup_name(name);
    node->file.size = 0;
    node->file.data = NULL;
    dir_add(parent, node);
    owner_fs->inode_count++;
    return node;
}

memfs_inode* memfs_create_device_buffer(memfs *fs, const char *path, void *buffer, size_t size, uint32_t flags) {
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *dev;
    if (!fs || !path || path[0] != '/' || !buffer || size == 0) return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return NULL;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_DEVICE) return existing->inode;
        return NULL;
    }
    dev = valloc(sizeof(memfs_inode));
    if (!dev) return NULL;
    memset(dev, 0, sizeof(memfs_inode));
    dev->type = MEMFS_TYPE_DEVICE;
    dev->name = dup_name(name);
    dev->device.buffer = (uint8_t*)buffer;
    dev->device.size = size;
    dev->device.flags = flags;
    dir_add(parent, dev);
    owner_fs->inode_count++;
    return dev;
}

memfs_inode* memfs_create_device_ops(
    memfs *fs, const char *path, uint32_t flags, memfs_dev_read_t read_cb,
    memfs_dev_write_t write_cb, memfs_dev_ioctl_t ioctl_cb, void *ctx
) {
    char name[256];
    memfs *owner_fs = fs;
    memfs_inode *parent;
    memfs_dentry *existing;
    memfs_inode *dev;
    if (!fs || !path || path[0] != '/') return NULL;
    parent = split_parent(fs, path, name, &owner_fs);
    if (!parent) return NULL;
    existing = lookup_dentry(parent, name);
    if (existing) {
        if (existing->mounted_fs) return NULL;
        if (existing->inode && existing->inode->type == MEMFS_TYPE_DEVICE) return existing->inode;
        return NULL;
    }
    dev = valloc(sizeof(memfs_inode));
    if (!dev) return NULL;
    memset(dev, 0, sizeof(memfs_inode));
    dev->type = MEMFS_TYPE_DEVICE;
    dev->name = dup_name(name);
    dev->device.flags = flags;
    dev->device.read = read_cb;
    dev->device.write = write_cb;
    dev->device.ioctl = ioctl_cb;
    dev->device.ctx = ctx;
    dir_add(parent, dev);
    owner_fs->inode_count++;
    return dev;
}

int memfs_delete_file(memfs *fs, const char *path) {
    memfs *owner_fs = fs;
    memfs_inode *node = lookup_path_ex(fs, path, &owner_fs);
    if (!node || !is_storage_node(node->type)) return -1;
    if (node->file.data) {
        vfree(node->file.data);
        used_sub(owner_fs, node->file.size);
        node->file.data = NULL;
        node->file.size = 0;
    }
    node->link_count--;
    return 0;
}

int memfs_delete_dir(memfs *fs, const char *path) {
    memfs *owner_fs = fs;
    memfs_dentry *d = lookup_path_dentry(fs, path, &owner_fs, NULL);
    memfs_inode *node;
    if (!d || !d->inode || d->inode->type != MEMFS_TYPE_DIR) return -1;
    if (d->mounted_fs) return -1;
    node = d->inode;
    if (node == owner_fs->root) return -1;
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
    memfs *owner_fs = fs;
    memfs_inode *node = lookup_path_ex(fs, path, &owner_fs);
    if (!node) return -1;
    if (node->type == MEMFS_TYPE_DEVICE) {
        if (!(node->device.flags & MEMFS_DEV_WRITE)) return -1;
        if (node->device.write) return node->device.write(node->device.ctx, buf, size);
        if (!node->device.buffer) return -1;
        {
            size_t to_copy = (size < node->device.size) ? size : node->device.size;
            memcpy(node->device.buffer, buf, to_copy);
            return (ssize_t)to_copy;
        }
    }
    if (!is_storage_node(node->type)) return -1;
    if (is_stream_node(node->type)) return stream_write(owner_fs, node, buf, size);
    if (node->file.data) {
        vfree(node->file.data);
        used_sub(owner_fs, node->file.size);
        node->file.data = NULL;
        node->file.size = 0;
    }
    node->file.data = valloc(size);
    if (!node->file.data) return -1;
    memcpy(node->file.data, buf, size);
    node->file.size = size;
    used_add(owner_fs, size);
    return (ssize_t)size;
}

ssize_t memfs_read(memfs *fs, const char *path, void *buf, size_t size) {
    memfs *owner_fs = fs;
    memfs_inode *node = lookup_path_ex(fs, path, &owner_fs);
    if (!node) return -1;
    if (node->type == MEMFS_TYPE_DEVICE) {
        if (!(node->device.flags & MEMFS_DEV_READ)) return -1;
        if (node->device.read) return node->device.read(node->device.ctx, buf, size);
        if (!node->device.buffer) return -1;
        {
            size_t to_copy = (size < node->device.size) ? size : node->device.size;
            memcpy(buf, node->device.buffer, to_copy);
            return (ssize_t)to_copy;
        }
    }
    if (!is_storage_node(node->type)) return -1;
    if (is_stream_node(node->type)) return stream_read(owner_fs, node, buf, size);
    {
        size_t to_copy = (size < node->file.size) ? size : node->file.size;
        memcpy(buf, node->file.data, to_copy);
        return (ssize_t)to_copy;
    }
}

int memfs_ioctl(memfs *fs, const char *path, uint32_t request, void *arg) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node || node->type != MEMFS_TYPE_DEVICE) return -1;
    if (!node->device.ioctl) return -1;
    return node->device.ioctl(node->device.ctx, request, arg);
}

int memfs_get_info(memfs *fs, const char *path, memfs_inode *out) {
    memfs_inode *node = lookup_path(fs, path);
    if (!node) return -1;
    memcpy(out, node, sizeof(memfs_inode));
    return 0;
}

memfs_inode* memfs_search(memfs *fs, const char *name) {
    memfs_inode *stack[128];
    int sp = 0;
    if (!fs) return NULL;
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
    memfs_inode *parent;
    char name[256];
    if (!old) return -1;
    parent = split_parent(fs, newpath, name, NULL);
    if (!parent) return -1;
    dir_add(parent, old);
    return 0;
}

ssize_t memfs_append(memfs *fs, const char *path, const void *buf, size_t size) {
    memfs *owner_fs = fs;
    memfs_inode *node = lookup_path_ex(fs, path, &owner_fs);
    uint8_t *newbuf;
    if (!node || !is_storage_node(node->type)) return -1;
    if (is_stream_node(node->type)) return stream_write(owner_fs, node, buf, size);
    newbuf = valloc(node->file.size + size);
    if (!newbuf) return -1;
    if (node->file.data) {
        memcpy(newbuf, node->file.data, node->file.size);
        vfree(node->file.data);
    }
    memcpy(newbuf + node->file.size, buf, size);
    used_add(owner_fs, size);
    node->file.size += size;
    node->file.data = newbuf;
    return (ssize_t)size;
}

size_t memfs_readdir(memfs *fs, const char *path, char **names, size_t max_entries) {
    memfs_inode *dir = lookup_path(fs, path);
    size_t count = 0;
    memfs_dentry *d;
    if (!dir || dir->type != MEMFS_TYPE_DIR) return 0;
    d = dir->dir.entries;
    while (d && count < max_entries) {
        names[count++] = d->name;
        d = d->next;
    }
    return count;
}

ssize_t memfs_ls_into(memfs *fs, const char *path, char *out, size_t out_size) {
    memfs_inode *dir = lookup_path(fs, path);
    size_t used = 0;
    size_t count = 0;
    memfs_dentry *d;
    if (!dir || dir->type != MEMFS_TYPE_DIR || !out || out_size == 0) return -1;
    d = dir->dir.entries;
    while (d) {
        size_t name_len = strlen(d->name);
        char suffix = d->inode ? type_suffix(d->inode->type) : '\0';
        size_t item_len = name_len + (suffix ? 1 : 0);
        size_t avail = out_size - 1 - used;
        if (count > 4096) return -1;
        if (item_len > avail) break;
        memcpy(out + used, d->name, name_len);
        used += name_len;
        if (suffix) out[used++] = suffix;
        if (d->next) {
            if (used >= out_size - 1) break;
            out[used++] = '\n';
        }
        d = d->next;
        count++;
    }
    out[used] = '\0';
    return (ssize_t)used;
}

char* memfs_ls(memfs *fs, const char *path) {
    memfs_inode *dir = lookup_path(fs, path);
    size_t total_len = 1;
    size_t count = 0;
    memfs_dentry *d;
    char *out;
    if (!dir || dir->type != MEMFS_TYPE_DIR) return NULL;
    d = dir->dir.entries;
    while (d) {
        total_len += strlen(d->name) + 2;
        d = d->next;
        if (++count > 4096) return NULL;
    }
    out = valloc(total_len);
    if (!out) return NULL;
    out[0] = '\0';
    d = dir->dir.entries;
    count = 0;
    while (d) {
        char suffix = d->inode ? type_suffix(d->inode->type) : '\0';
        strcat(out, d->name);
        if (suffix) {
            size_t l = strlen(out);
            out[l] = suffix;
            out[l + 1] = '\0';
        }
        if (d->next) strcat(out, "\n");
        d = d->next;
        if (++count > 4096) return NULL;
    }
    return out;
}

int memfs_mount(memfs *target_fs, const char *mount_path, memfs *mounted_fs) {
    const char *last;
    char parent_path[256];
    size_t parent_len;
    memfs_inode *parent;
    memfs_dentry *d;
    if (!target_fs || !mount_path || !mounted_fs) return -1;
    if (mount_path[0] != '/' || strcmp(mount_path, "/") == 0) return -1;
    if (!mounted_fs->root || mounted_fs->root->type != MEMFS_TYPE_DIR) return -1;
    last = strrchr(mount_path, '/');
    if (!last || *(last + 1) == '\0') return -1;
    parent_len = (size_t)(last - mount_path);
    if (parent_len == 0) strcpy(parent_path, "/");
    else {
        memcpy(parent_path, mount_path, parent_len);
        parent_path[parent_len] = '\0';
    }
    parent = lookup_path(target_fs, parent_path);
    if (!parent || parent->type != MEMFS_TYPE_DIR) return -1;
    d = lookup_dentry(parent, last + 1);
    if (!d || !d->inode || d->inode->type != MEMFS_TYPE_DIR) return -1;
    if (d->mounted_fs) return -1;
    d->mounted_fs = mounted_fs;
    return 0;
}

int memfs_umount(memfs *target_fs, const char *mount_path) {
    memfs_dentry *d;
    if (!target_fs || !mount_path) return -1;
    if (mount_path[0] != '/' || strcmp(mount_path, "/") == 0) return -1;
    d = lookup_path_dentry(target_fs, mount_path, NULL, NULL);
    if (!d || !d->mounted_fs) return -1;
    d->mounted_fs = NULL;
    return 0;
}
