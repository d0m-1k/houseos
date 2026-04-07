#pragma once

#include <stdint.h>
#include <sys/types.h>

#define S_IFMT  0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFSOCK 0140000

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

struct stat {
    mode_t st_mode;
    off_t st_size;
};

int32_t mkdir(const char *path, ...);
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
