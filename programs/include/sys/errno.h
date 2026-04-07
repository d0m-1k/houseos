#pragma once

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define ENOSYS 38
#define ENOTSUP 95
#define ETIMEDOUT 110
#define ESRCH 3

extern int errno;
