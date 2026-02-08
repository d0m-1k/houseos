#pragma once

#include <drivers/filesystem/memfs.h>

#define INITRAMFS_ADDR 0x40000

#define CPIO_S_IFMT   0170000
#define CPIO_S_IFDIR  0040000
#define CPIO_S_IFREG  0100000
#define CPIO_S_IFLNK  0120000
#define CPIO_S_ISUID  0004000
#define CPIO_S_ISGID  0002000
#define CPIO_S_ISVTX  0001000

#define CPIO_S_ISDIR(mode) (((mode) & CPIO_S_IFMT) == CPIO_S_IFDIR)
#define CPIO_S_ISREG(mode) (((mode) & CPIO_S_IFMT) == CPIO_S_IFREG)
#define CPIO_S_ISLNK(mode) (((mode) & CPIO_S_IFMT) == CPIO_S_IFLNK)
#define CPIO_S_ISCHR(mode) (((mode) & CPIO_S_IFMT) == 0020000)
#define CPIO_S_ISBLK(mode) (((mode) & CPIO_S_IFMT) == 0060000)
#define CPIO_S_ISFIFO(mode) (((mode) & CPIO_S_IFMT) == 0010000)
#define CPIO_S_ISSOCK(mode) (((mode) & CPIO_S_IFMT) == 0140000)

struct cpio_header {
    char magic[6];
    char inode[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
} __attribute__((packed));

void initramfs_init(memfs *fs);