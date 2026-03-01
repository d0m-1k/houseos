#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct _memfs_inode memfs_inode;
typedef struct _memfs_dentry memfs_dentry;
typedef struct _memfs memfs;

typedef ssize_t (*memfs_dev_read_t)(void *ctx, void *buf, size_t size);
typedef ssize_t (*memfs_dev_write_t)(void *ctx, const void *buf, size_t size);
typedef int (*memfs_dev_ioctl_t)(void *ctx, uint32_t request, void *arg);

typedef enum _memfs_node_type {
    MEMFS_TYPE_FILE,
    MEMFS_TYPE_DIR,
    MEMFS_TYPE_SYMLINK,
    MEMFS_TYPE_DEVICE,
    MEMFS_TYPE_FIFO,
    MEMFS_TYPE_SOCKET,
    MEMFS_TYPE_CHARDEV,
    MEMFS_TYPE_BLOCKDEV
} memfs_node_type;

enum {
    MEMFS_DEV_READ = 1 << 0,
    MEMFS_DEV_WRITE = 1 << 1,
};

typedef struct _memfs_dentry {
    char *name;
    memfs_inode *inode;
    memfs *mounted_fs;
    memfs_dentry *next;
} memfs_dentry;

typedef struct _memfs_inode {
    memfs_node_type type;
    char *name;
    uint32_t mode;
    uint16_t uid;
    uint16_t gid;

    uint32_t link_count;

    union {
        struct {
            size_t size;
            uint8_t *data;
        } file;

        struct {
            memfs_dentry *entries;
            size_t entry_count;
            memfs_inode *parent;
        } dir;

        struct {
            char *target;
        } symlink;

        struct {
            uint8_t *buffer;
            size_t size;
            uint32_t flags;
            memfs_dev_read_t read;
            memfs_dev_write_t write;
            memfs_dev_ioctl_t ioctl;
            void *ctx;
        } device;
    };

    memfs_inode *next;
    memfs_inode *prev;
} memfs_inode;

struct _memfs {
    memfs_inode *root;

    size_t total_memory;
    size_t used_memory;
    size_t inode_count;
};

memfs* memfs_create(size_t size);

memfs_inode* lookup_path(memfs *fs, const char *path);

memfs_inode* memfs_create_dir(memfs *fs, const char *path);
memfs_inode* memfs_create_file(memfs *fs, const char *path);
memfs_inode* memfs_create_fifo(memfs *fs, const char *path);
memfs_inode* memfs_create_socket(memfs *fs, const char *path);
memfs_inode* memfs_create_device_buffer(memfs *fs, const char *path, void *buffer, size_t size, uint32_t flags);
memfs_inode* memfs_create_device_ops(memfs *fs, const char *path, uint32_t flags, memfs_dev_read_t read_cb, memfs_dev_write_t write_cb, memfs_dev_ioctl_t ioctl_cb, void *ctx);

int memfs_delete_dir(memfs *fs, const char *path);
int memfs_delete_file(memfs *fs, const char *path);

int memfs_open(memfs *fs, const char *path);
int memfs_close(int fd);

ssize_t memfs_write(memfs *fs, const char *path, const void *buf, size_t size);
ssize_t memfs_read(memfs *fs, const char *path, void *buf, size_t size);
int memfs_ioctl(memfs *fs, const char *path, uint32_t request, void *arg);

int memfs_get_info(memfs *fs, const char *path, memfs_inode *out);
memfs_inode* memfs_search(memfs *fs, const char *name);
int memfs_link(memfs *fs, const char *oldpath, const char *newpath);
ssize_t memfs_append(memfs *fs, const char *path, const void *buf, size_t size);

size_t memfs_readdir(memfs *fs, const char *path, char **names, size_t max_entries);
ssize_t memfs_ls_into(memfs *fs, const char *path, char *out, size_t out_size);
char* memfs_ls(memfs *fs, const char *path);
int memfs_mount(memfs *target_fs, const char *mount_path, memfs *mounted_fs);
int memfs_umount(memfs *target_fs, const char *mount_path);
