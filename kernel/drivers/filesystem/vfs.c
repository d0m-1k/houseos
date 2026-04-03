#include <drivers/filesystem/vfs.h>
#include <string.h>

static int is_valid_abs_path(const char *path) {
    return (path && path[0] == '/');
}

static int is_root_path(const char *path) {
    return (path && path[0] == '/' && path[1] == '\0');
}

static int normalize_mount_path(const char *in, char *out, uint32_t cap) {
    size_t n;
    if (!in || !out || cap < 2) return -1;
    if (in[0] != '/') return -1;
    n = strlen(in);
    if (n == 0 || n >= cap) return -1;
    if (n > 1 && in[n - 1] == '/') n--;
    memcpy(out, in, n);
    out[n] = '\0';
    return 0;
}

static int path_starts_with_mount(const char *path, const char *mount) {
    size_t mlen;
    if (!path || !mount) return 0;
    mlen = strlen(mount);
    if (mlen == 0) return 0;
    if (strcmp(mount, "/") == 0) return path[0] == '/';
    if (strncmp(path, mount, mlen) != 0) return 0;
    return (path[mlen] == '\0' || path[mlen] == '/');
}

static int make_local_path(const char *path, const char *mount, char *out, uint32_t cap) {
    size_t mlen;
    const char *suffix;
    if (!path || !mount || !out || cap < 2) return -1;
    if (strcmp(mount, "/") == 0) {
        strncpy(out, path, cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    mlen = strlen(mount);
    suffix = path + mlen;
    if (suffix[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    strncpy(out, suffix, cap - 1);
    out[cap - 1] = '\0';
    return 0;
}

static int is_exact_mount_path(vfs_t *vfs, const char *path) {
    char norm[256];
    if (!vfs || !path) return 0;
    if (normalize_mount_path(path, norm, sizeof(norm)) != 0) return 0;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        if (strcmp(vfs->mounts[i].mount_path, norm) == 0) return 1;
    }
    return 0;
}

static int list_has_entry_name(const char *list, const char *name) {
    uint32_t i = 0;
    uint32_t nlen;
    if (!list || !name) return 0;
    nlen = (uint32_t)strlen(name);
    if (nlen == 0) return 0;

    while (list[i]) {
        uint32_t start = i;
        uint32_t tok_len = 0;
        while (list[i] && list[i] != '\n') i++;
        while (tok_len < nlen && list[start + tok_len] == name[tok_len]) tok_len++;
        if (tok_len == nlen) {
            char c = list[start + tok_len];
            if (c == '/' || c == '*' || c == '\n' || c == '\0') return 1;
        }
        if (list[i] == '\n') i++;
    }
    return 0;
}

static int mount_direct_child_name(const char *dir_path, const char *mount_path, char *name_out, uint32_t cap) {
    const char *rest;
    uint32_t i = 0;
    if (!dir_path || !mount_path || !name_out || cap < 2) return -1;
    if (strcmp(dir_path, "/") == 0) {
        if (mount_path[0] != '/' || mount_path[1] == '\0') return -1;
        rest = mount_path + 1;
    } else {
        uint32_t dlen = (uint32_t)strlen(dir_path);
        if (strncmp(mount_path, dir_path, dlen) != 0) return -1;
        if (mount_path[dlen] != '/' || mount_path[dlen + 1] == '\0') return -1;
        rest = mount_path + dlen + 1;
    }

    while (rest[i] && rest[i] != '/') {
        if (i + 1 >= cap) return -1;
        name_out[i] = rest[i];
        i++;
    }
    if (i == 0 || rest[i] != '\0') return -1;
    name_out[i] = '\0';
    return 0;
}

static const vfs_ops_t* find_fs_ops(vfs_t *vfs, const char *fs_name) {
    if (!vfs || !fs_name || fs_name[0] == '\0') return NULL;
    for (uint32_t i = 0; i < vfs->fs_driver_count; i++) {
        if (strcmp(vfs->fs_drivers[i].name, fs_name) == 0) return vfs->fs_drivers[i].ops;
    }
    return NULL;
}

int vfs_init(vfs_t *vfs) {
    if (!vfs) return -1;
    memset(vfs, 0, sizeof(*vfs));
    return 0;
}

int vfs_register_fs(vfs_t *vfs, const char *fs_name, const vfs_ops_t *ops) {
    size_t n;
    if (!vfs || !fs_name || !ops) return -1;
    n = strlen(fs_name);
    if (n == 0 || n >= VFS_FS_NAME_MAX) return -1;
    for (uint32_t i = 0; i < vfs->fs_driver_count; i++) {
        if (strcmp(vfs->fs_drivers[i].name, fs_name) == 0) return -1;
    }
    if (vfs->fs_driver_count >= VFS_MAX_FS_DRIVERS) return -1;
    strncpy(vfs->fs_drivers[vfs->fs_driver_count].name, fs_name, VFS_FS_NAME_MAX - 1);
    vfs->fs_drivers[vfs->fs_driver_count].name[VFS_FS_NAME_MAX - 1] = '\0';
    vfs->fs_drivers[vfs->fs_driver_count].ops = ops;
    vfs->fs_driver_count++;
    return 0;
}

int vfs_set_root(vfs_t *vfs, const char *fs_name, void *fs_ctx) {
    const vfs_ops_t *ops;
    if (!vfs || !fs_ctx) return -1;
    ops = find_fs_ops(vfs, fs_name);
    if (!ops) return -1;
    vfs->root_fs = fs_ctx;
    vfs->root_ops = ops;
    strncpy(vfs->root_source, fs_name, sizeof(vfs->root_source) - 1);
    vfs->root_source[sizeof(vfs->root_source) - 1] = '\0';
    return 0;
}

int vfs_set_root_source(vfs_t *vfs, const char *source) {
    if (!vfs || !source || source[0] == '\0') return -1;
    strncpy(vfs->root_source, source, sizeof(vfs->root_source) - 1);
    vfs->root_source[sizeof(vfs->root_source) - 1] = '\0';
    return 0;
}

int vfs_mount(vfs_t *vfs, const char *mount_path, const char *fs_name, void *fs_ctx) {
    char norm[256];
    const vfs_ops_t *ops;
    vfs_resolved_t r;
    vfs_info_t info;
    if (!vfs || !mount_path || !fs_ctx) return -1;
    if (vfs->mount_count >= VFS_MAX_MOUNTS) return -1;
    ops = find_fs_ops(vfs, fs_name);
    if (!ops) return -1;
    if (normalize_mount_path(mount_path, norm, sizeof(norm)) != 0) return -1;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        if (strcmp(vfs->mounts[i].mount_path, norm) == 0) return -1;
    }

    /* mountpoint must already exist and be a directory */
    if (vfs_resolve(vfs, norm, &r) != 0 || !r.ops || !r.ops->get_info) return -1;
    if (r.ops->get_info(r.fs_ctx, r.local_path, &info) != 0) return -1;
    if (info.type != VFS_NODE_DIR) return -1;

    strncpy(vfs->mounts[vfs->mount_count].mount_path, norm, sizeof(vfs->mounts[0].mount_path) - 1);
    vfs->mounts[vfs->mount_count].mount_path[sizeof(vfs->mounts[0].mount_path) - 1] = '\0';
    strncpy(vfs->mounts[vfs->mount_count].source, fs_name, sizeof(vfs->mounts[0].source) - 1);
    vfs->mounts[vfs->mount_count].source[sizeof(vfs->mounts[0].source) - 1] = '\0';
    vfs->mounts[vfs->mount_count].fs_ctx = fs_ctx;
    vfs->mounts[vfs->mount_count].ops = ops;
    vfs->mount_count++;
    return 0;
}

int vfs_set_mount_source(vfs_t *vfs, const char *mount_path, const char *source) {
    char norm[256];
    if (!vfs || !mount_path || !source || source[0] == '\0') return -1;
    if (normalize_mount_path(mount_path, norm, sizeof(norm)) != 0) return -1;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        if (strcmp(vfs->mounts[i].mount_path, norm) == 0) {
            strncpy(vfs->mounts[i].source, source, sizeof(vfs->mounts[i].source) - 1);
            vfs->mounts[i].source[sizeof(vfs->mounts[i].source) - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int vfs_umount(vfs_t *vfs, const char *mount_path) {
    char norm[256];
    uint32_t idx = VFS_MAX_MOUNTS;
    if (!vfs || !mount_path) return -1;
    if (normalize_mount_path(mount_path, norm, sizeof(norm)) != 0) return -1;
    if (strcmp(norm, "/") == 0) return -1;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        if (strcmp(vfs->mounts[i].mount_path, norm) == 0) {
            idx = i;
            break;
        }
    }
    if (idx >= vfs->mount_count) return -1;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        size_t nlen;
        if (i == idx) continue;
        nlen = strlen(norm);
        if (strncmp(vfs->mounts[i].mount_path, norm, nlen) == 0 &&
            vfs->mounts[i].mount_path[nlen] == '/') {
            return -1;
        }
    }
    for (uint32_t i = idx + 1; i < vfs->mount_count; i++) {
        vfs->mounts[i - 1] = vfs->mounts[i];
    }
    memset(&vfs->mounts[vfs->mount_count - 1], 0, sizeof(vfs->mounts[0]));
    vfs->mount_count--;
    return 0;
}

int vfs_resolve(vfs_t *vfs, const char *path, vfs_resolved_t *out) {
    int best = -1;
    size_t best_len = 0;
    if (!vfs || !path || !out || !is_valid_abs_path(path)) return -1;
    if (!vfs->root_fs || !vfs->root_ops) return -1;

    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        const char *m = vfs->mounts[i].mount_path;
        size_t mlen = strlen(m);
        if (path_starts_with_mount(path, m) && mlen > best_len) {
            best = (int)i;
            best_len = mlen;
        }
    }

    if (best >= 0) {
        out->fs_ctx = vfs->mounts[best].fs_ctx;
        out->ops = vfs->mounts[best].ops;
        if (make_local_path(path, vfs->mounts[best].mount_path, out->local_path, sizeof(out->local_path)) != 0) return -1;
        return 0;
    }

    out->fs_ctx = vfs->root_fs;
    out->ops = vfs->root_ops;
    strncpy(out->local_path, path, sizeof(out->local_path) - 1);
    out->local_path[sizeof(out->local_path) - 1] = '\0';
    return 0;
}

int vfs_open(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->open) return -1;
    return r.ops->open(r.fs_ctx, r.local_path);
}

int vfs_close(vfs_t *vfs, int fd) {
    if (!vfs || !vfs->root_ops || !vfs->root_ops->close) return -1;
    return vfs->root_ops->close(vfs->root_fs, fd);
}

ssize_t vfs_read(vfs_t *vfs, const char *path, void *buf, size_t size) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->read) return -1;
    return r.ops->read(r.fs_ctx, r.local_path, buf, size);
}

ssize_t vfs_write(vfs_t *vfs, const char *path, const void *buf, size_t size) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->write) return -1;
    return r.ops->write(r.fs_ctx, r.local_path, buf, size);
}

ssize_t vfs_append(vfs_t *vfs, const char *path, const void *buf, size_t size) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->append) return -1;
    return r.ops->append(r.fs_ctx, r.local_path, buf, size);
}

int vfs_ioctl(vfs_t *vfs, const char *path, uint32_t request, void *arg) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->ioctl) return -1;
    return r.ops->ioctl(r.fs_ctx, r.local_path, request, arg);
}

int vfs_mkdir(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    if (is_exact_mount_path(vfs, path)) return -1;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->mkdir) return -1;
    return r.ops->mkdir(r.fs_ctx, r.local_path);
}

int vfs_create_file(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    if (is_exact_mount_path(vfs, path)) return -1;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->create_file) return -1;
    return r.ops->create_file(r.fs_ctx, r.local_path);
}

int vfs_link(vfs_t *vfs, const char *oldpath, const char *newpath) {
    vfs_resolved_t ro;
    vfs_resolved_t rn;
    if (is_exact_mount_path(vfs, oldpath) || is_exact_mount_path(vfs, newpath)) return -1;
    if (vfs_resolve(vfs, oldpath, &ro) != 0 || !ro.ops || !ro.ops->link) return -1;
    if (vfs_resolve(vfs, newpath, &rn) != 0 || !rn.ops || !rn.ops->link) return -1;
    if (ro.ops != rn.ops || ro.fs_ctx != rn.fs_ctx) return -1;
    return ro.ops->link(ro.fs_ctx, ro.local_path, rn.local_path);
}

int vfs_unlink(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    vfs_info_t info;
    if (is_exact_mount_path(vfs, path)) return -1;
    if (is_root_path(path)) return -1;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->unlink) return -1;
    if (!r.ops->get_info) return -1;
    if (r.ops->get_info(r.fs_ctx, r.local_path, &info) != 0) return -1;
    if (info.type == VFS_NODE_DIR) return -1;
    return r.ops->unlink(r.fs_ctx, r.local_path);
}

int vfs_rmdir(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    vfs_info_t info;
    char norm[256];
    if (is_exact_mount_path(vfs, path)) return -1;
    if (is_root_path(path)) return -1;
    if (normalize_mount_path(path, norm, sizeof(norm)) != 0) return -1;
    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        size_t nlen = strlen(norm);
        if (strncmp(vfs->mounts[i].mount_path, norm, nlen) == 0 &&
            vfs->mounts[i].mount_path[nlen] == '/') {
            return -1;
        }
    }
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->rmdir) return -1;
    if (!r.ops->get_info) return -1;
    if (r.ops->get_info(r.fs_ctx, r.local_path, &info) != 0) return -1;
    if (info.type != VFS_NODE_DIR) return -1;
    return r.ops->rmdir(r.fs_ctx, r.local_path);
}

int vfs_mkfifo(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    if (is_exact_mount_path(vfs, path)) return -1;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->mkfifo) return -1;
    return r.ops->mkfifo(r.fs_ctx, r.local_path);
}

int vfs_mksock(vfs_t *vfs, const char *path) {
    vfs_resolved_t r;
    if (is_exact_mount_path(vfs, path)) return -1;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->mksock) return -1;
    return r.ops->mksock(r.fs_ctx, r.local_path);
}

ssize_t vfs_list(vfs_t *vfs, const char *path, char *out, size_t out_size) {
    vfs_resolved_t r;
    char norm[256];
    ssize_t n;
    uint32_t pos;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->list) return -1;
    n = r.ops->list(r.fs_ctx, r.local_path, out, out_size);
    if (n < 0) return -1;
    pos = (uint32_t)n;
    if (normalize_mount_path(path, norm, sizeof(norm)) != 0) return n;

    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        char child[256];
        uint32_t clen;
        if (mount_direct_child_name(norm, vfs->mounts[i].mount_path, child, sizeof(child)) != 0) continue;
        if (list_has_entry_name(out, child)) continue;
        clen = (uint32_t)strlen(child);
        if (pos + clen + 2 >= out_size) break;
        memcpy(out + pos, child, clen);
        pos += clen;
        out[pos++] = '/';
        out[pos++] = '\n';
        out[pos] = '\0';
    }
    return (ssize_t)pos;
}

ssize_t vfs_list_mounts(vfs_t *vfs, char *out, size_t out_size) {
    uint32_t pos = 0;
    const char *root_name = "unknown";
    if (!vfs || !out || out_size == 0) return -1;
    out[0] = '\0';

    for (uint32_t i = 0; i < vfs->fs_driver_count; i++) {
        if (vfs->fs_drivers[i].ops == vfs->root_ops) {
            root_name = vfs->fs_drivers[i].name;
            break;
        }
    }

    {
        const char *a = root_name;
        const char *src = vfs->root_source[0] ? vfs->root_source : root_name;
        size_t alen = strlen(a);
        size_t slen = strlen(src);
        if (pos + alen + 1 + slen + 1 + 1 + 1 >= out_size) return -1;
        memcpy(out + pos, a, alen);
        pos += (uint32_t)alen;
        out[pos++] = ' ';
        memcpy(out + pos, src, slen);
        pos += (uint32_t)slen;
        out[pos++] = ' ';
        out[pos++] = '/';
        out[pos++] = '\n';
        out[pos] = '\0';
    }

    for (uint32_t i = 0; i < vfs->mount_count; i++) {
        const char *fs_name = "unknown";
        const char *path = vfs->mounts[i].mount_path;
        size_t nlen;
        size_t plen;
        for (uint32_t d = 0; d < vfs->fs_driver_count; d++) {
            if (vfs->fs_drivers[d].ops == vfs->mounts[i].ops) {
                fs_name = vfs->fs_drivers[d].name;
                break;
            }
        }

        nlen = strlen(fs_name);
        plen = strlen(path);
        {
            const char *src = vfs->mounts[i].source[0] ? vfs->mounts[i].source : fs_name;
            size_t slen = strlen(src);
            if (pos + nlen + 1 + slen + 1 + plen + 1 + 1 >= out_size) return -1;
            memcpy(out + pos, fs_name, nlen);
            pos += (uint32_t)nlen;
            out[pos++] = ' ';
            memcpy(out + pos, src, slen);
            pos += (uint32_t)slen;
            out[pos++] = ' ';
            memcpy(out + pos, path, plen);
            pos += (uint32_t)plen;
            out[pos++] = '\n';
            out[pos] = '\0';
        }
    }

    return (ssize_t)pos;
}

int vfs_get_info(vfs_t *vfs, const char *path, vfs_info_t *out) {
    vfs_resolved_t r;
    if (vfs_resolve(vfs, path, &r) != 0 || !r.ops || !r.ops->get_info) return -1;
    return r.ops->get_info(r.fs_ctx, r.local_path, out);
}
