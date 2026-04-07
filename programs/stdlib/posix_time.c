#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <syscall.h>

#define TICKS_HZ 100u

static int ticks_to_timespec(uint32_t ticks, struct timespec *tp) {
    if (!tp) return -1;
    tp->tv_sec = (time_t)(ticks / TICKS_HZ);
    tp->tv_nsec = (long)((ticks % TICKS_HZ) * (1000000000u / TICKS_HZ));
    return 0;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    uint32_t ticks;
    if (!tp) {
        errno = EINVAL;
        return -1;
    }
    if (clk_id != CLOCK_MONOTONIC && clk_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    ticks = get_ticks();
    return ticks_to_timespec(ticks, tp);
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    struct timespec ts;
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    tv->tv_sec = (long)ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    uint64_t usec64;
    useconds_t usec;
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    usec64 = (uint64_t)req->tv_sec * 1000000ull + (uint64_t)(req->tv_nsec / 1000L);
    if (usec64 > 0xFFFFFFFFu) usec = 0xFFFFFFFFu;
    else usec = (useconds_t)usec64;
    return usleep(usec);
}
