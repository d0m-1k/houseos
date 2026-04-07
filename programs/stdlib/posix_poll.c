#include <poll.h>
#include <sys/select.h>
#include <syscall.h>
#include <errno.h>

typedef struct {
    int32_t nfds;
    uint32_t *readfds;
    uint32_t *writefds;
    uint32_t *exceptfds;
    int32_t timeout_ms;
} syscall_select_req_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    int32_t r = sys_poll_raw(fds, nfds, timeout);
    if (r < 0) return -1;
    return r;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    syscall_select_req_t req;
    int32_t timeout_ms = -1;
    if (nfds < 0 || nfds > 32) {
        errno = EINVAL;
        return -1;
    }
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0) {
            errno = EINVAL;
            return -1;
        }
        timeout_ms = (int32_t)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    }
    req.nfds = nfds;
    req.readfds = readfds ? &readfds->bits : 0;
    req.writefds = writefds ? &writefds->bits : 0;
    req.exceptfds = exceptfds ? &exceptfds->bits : 0;
    req.timeout_ms = timeout_ms;
    return sys_select_raw(&req);
}
