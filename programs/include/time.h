#pragma once

#include <sys/types.h>

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

typedef long time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int clock_gettime(int clk_id, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);
