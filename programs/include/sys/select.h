#pragma once

#include <stdint.h>
#include <sys/time.h>

typedef struct {
    uint32_t bits;
} fd_set;

#define FD_ZERO(set) do { (set)->bits = 0u; } while (0)
#define FD_SET(fd, set) do { (set)->bits |= (1u << (fd)); } while (0)
#define FD_CLR(fd, set) do { (set)->bits &= ~(1u << (fd)); } while (0)
#define FD_ISSET(fd, set) (((set)->bits & (1u << (fd))) != 0u)

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
