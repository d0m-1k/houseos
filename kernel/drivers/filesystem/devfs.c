#include <drivers/filesystem/devfs.h>
#include <string.h>

int devfs_init(devfs_t *devfs, size_t mem_size) {
    if (!devfs) return -1;
    devfs->fs = memfs_create(mem_size);
    return devfs->fs ? 0 : -1;
}

int devfs_create_dir(devfs_t *devfs, const char *path) {
    if (!devfs || !devfs->fs || !path || path[0] != '/') return -1;
    return memfs_create_dir(devfs->fs, path) ? 0 : -1;
}

int devfs_create_device_ops(
    devfs_t *devfs, const char *path, uint32_t flags,
    memfs_dev_read_t read_cb, memfs_dev_write_t write_cb, memfs_dev_ioctl_t ioctl_cb, void *ctx
) {
    if (!devfs || !devfs->fs || !path || path[0] != '/') return -1;
    return memfs_create_device_ops(devfs->fs, path, flags, read_cb, write_cb, ioctl_cb, ctx) ? 0 : -1;
}

static int devfs_open_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_open(d->fs, path);
}

static int devfs_close_op(void *fs_ctx, int fd) {
    (void)fs_ctx;
    return memfs_close(fd);
}

static ssize_t devfs_read_op(void *fs_ctx, const char *path, void *buf, size_t size) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_read(d->fs, path, buf, size);
}

static ssize_t devfs_write_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_write(d->fs, path, buf, size);
}

static ssize_t devfs_append_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_append(d->fs, path, buf, size);
}

static int devfs_ioctl_op(void *fs_ctx, const char *path, uint32_t request, void *arg) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_ioctl(d->fs, path, request, arg);
}

static int devfs_mkdir_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_create_dir(d->fs, path) ? 0 : -1;
}

static int devfs_create_file_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_create_file(d->fs, path) ? 0 : -1;
}

static int devfs_link_op(void *fs_ctx, const char *oldpath, const char *newpath) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_link(d->fs, oldpath, newpath);
}

static int devfs_unlink_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_delete_file(d->fs, path);
}

static int devfs_rmdir_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_delete_dir(d->fs, path);
}

static int devfs_mkfifo_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_create_fifo(d->fs, path) ? 0 : -1;
}

static int devfs_mksock_op(void *fs_ctx, const char *path) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_create_socket(d->fs, path) ? 0 : -1;
}

static ssize_t devfs_list_op(void *fs_ctx, const char *path, char *out, size_t out_size) {
    devfs_t *d = (devfs_t*)fs_ctx;
    if (!d || !d->fs) return -1;
    return memfs_ls_into(d->fs, path, out, out_size);
}

static int devfs_get_info_op(void *fs_ctx, const char *path, vfs_info_t *out) {
    devfs_t *d = (devfs_t*)fs_ctx;
    memfs_inode in;
    if (!d || !d->fs || !out) return -1;
    if (memfs_get_info(d->fs, path, &in) != 0) return -1;
    out->type = (vfs_node_type_t)in.type;
    out->mode = in.mode;
    out->uid = in.uid;
    out->gid = in.gid;
    out->size = in.file.size;
    return 0;
}

const vfs_ops_t g_devfs_vfs_ops = {
    devfs_open_op,
    devfs_close_op,
    devfs_read_op,
    devfs_write_op,
    devfs_append_op,
    devfs_ioctl_op,
    devfs_mkdir_op,
    devfs_create_file_op,
    devfs_link_op,
    devfs_unlink_op,
    devfs_rmdir_op,
    devfs_mkfifo_op,
    devfs_mksock_op,
    devfs_list_op,
    devfs_get_info_op
};
