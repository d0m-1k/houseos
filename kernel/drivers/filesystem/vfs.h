#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VFS_NODE_FILE = 0,
    VFS_NODE_DIR = 1,
    VFS_NODE_SYMLINK = 2,
    VFS_NODE_DEVICE = 3,
    VFS_NODE_FIFO = 4,
    VFS_NODE_SOCKET = 5,
    VFS_NODE_CHARDEV = 6,
    VFS_NODE_BLOCKDEV = 7
} vfs_node_type_t;

typedef struct {
    vfs_node_type_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
} vfs_info_t;

typedef struct _vfs_ops {
    int (*open)(void *fs_ctx, const char *path);
    int (*close)(void *fs_ctx, int fd);
    ssize_t (*read)(void *fs_ctx, const char *path, void *buf, size_t size);
    ssize_t (*write)(void *fs_ctx, const char *path, const void *buf, size_t size);
    ssize_t (*append)(void *fs_ctx, const char *path, const void *buf, size_t size);
    int (*ioctl)(void *fs_ctx, const char *path, uint32_t request, void *arg);
    int (*mkdir)(void *fs_ctx, const char *path);
    int (*create_file)(void *fs_ctx, const char *path);
    int (*link)(void *fs_ctx, const char *oldpath, const char *newpath);
    int (*unlink)(void *fs_ctx, const char *path);
    int (*rmdir)(void *fs_ctx, const char *path);
    int (*mkfifo)(void *fs_ctx, const char *path);
    int (*mksock)(void *fs_ctx, const char *path);
    ssize_t (*list)(void *fs_ctx, const char *path, char *out, size_t out_size);
    int (*get_info)(void *fs_ctx, const char *path, vfs_info_t *out);
} vfs_ops_t;

#define VFS_FS_NAME_MAX 16
#define VFS_MAX_FS_DRIVERS 8
#define VFS_MAX_MOUNTS 8

typedef struct {
    char name[VFS_FS_NAME_MAX];
    const vfs_ops_t *ops;
} vfs_fs_driver_t;

typedef struct {
    char mount_path[256];
    char source[256];
    void *fs_ctx;
    const vfs_ops_t *ops;
} vfs_mount_t;

typedef struct {
    void *root_fs;
    const vfs_ops_t *root_ops;
    char root_source[256];
    vfs_fs_driver_t fs_drivers[VFS_MAX_FS_DRIVERS];
    uint32_t fs_driver_count;
    vfs_mount_t mounts[VFS_MAX_MOUNTS];
    uint32_t mount_count;
} vfs_t;

typedef struct {
    void *fs_ctx;
    const vfs_ops_t *ops;
    char local_path[256];
} vfs_resolved_t;

int vfs_init(vfs_t *vfs);
int vfs_register_fs(vfs_t *vfs, const char *fs_name, const vfs_ops_t *ops);
int vfs_set_root(vfs_t *vfs, const char *fs_name, void *fs_ctx);
int vfs_set_root_source(vfs_t *vfs, const char *source);
int vfs_mount(vfs_t *vfs, const char *mount_path, const char *fs_name, void *fs_ctx);
int vfs_set_mount_source(vfs_t *vfs, const char *mount_path, const char *source);
int vfs_umount(vfs_t *vfs, const char *mount_path);

int vfs_resolve(vfs_t *vfs, const char *path, vfs_resolved_t *out);

int vfs_open(vfs_t *vfs, const char *path);
int vfs_close(vfs_t *vfs, int fd);
ssize_t vfs_read(vfs_t *vfs, const char *path, void *buf, size_t size);
ssize_t vfs_write(vfs_t *vfs, const char *path, const void *buf, size_t size);
ssize_t vfs_append(vfs_t *vfs, const char *path, const void *buf, size_t size);
int vfs_ioctl(vfs_t *vfs, const char *path, uint32_t request, void *arg);
int vfs_mkdir(vfs_t *vfs, const char *path);
int vfs_create_file(vfs_t *vfs, const char *path);
int vfs_link(vfs_t *vfs, const char *oldpath, const char *newpath);
int vfs_unlink(vfs_t *vfs, const char *path);
int vfs_rmdir(vfs_t *vfs, const char *path);
int vfs_mkfifo(vfs_t *vfs, const char *path);
int vfs_mksock(vfs_t *vfs, const char *path);
ssize_t vfs_list(vfs_t *vfs, const char *path, char *out, size_t out_size);
ssize_t vfs_list_mounts(vfs_t *vfs, char *out, size_t out_size);
int vfs_get_info(vfs_t *vfs, const char *path, vfs_info_t *out);
