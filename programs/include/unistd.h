#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int32_t close(int fd);
int32_t dup(int oldfd);
int32_t dup2(int oldfd, int newfd);
int32_t unlink(const char *path);
int32_t rmdir(const char *path);
int32_t link(const char *oldpath, const char *newpath);
pid_t getpid(void);
pid_t getppid(void);
int access(const char *path, int mode);
int isatty(int fd);
int usleep(useconds_t usec);
pid_t fork(void);
int pipe(int fd[2]);
off_t lseek(int fd, off_t offset, int whence);
pid_t getuid(void);
pid_t geteuid(void);
pid_t getgid(void);
pid_t getegid(void);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg0, ...);
int fsync(int fd);
