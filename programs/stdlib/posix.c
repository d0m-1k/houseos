#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syscall.h>
#include <devctl.h>
#include <errno.h>

int creat(const char *path, mode_t mode) {
    (void)mode;
    return open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int access(const char *path, int mode) {
    int fd;
    char tmp[4];
    (void)mode;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (list(path, tmp, sizeof(tmp)) >= 0) return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

int isatty(int fd) {
    dev_tty_info_t ti;
    if (fd < 0) {
        errno = EBADF;
        return 0;
    }
    if (ioctl(fd, DEV_IOCTL_TTY_GET_INFO, &ti) != 0) return 0;
    return 1;
}

int usleep(useconds_t usec) {
    uint32_t ms;
    if (usec == 0) return 0;
    ms = (uint32_t)((usec + 999u) / 1000u);
    sleep(ms ? ms : 1u);
    return 0;
}
