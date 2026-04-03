#pragma once

#include <stdint.h>

#define INADDR_ANY       0x00000000u
#define INADDR_LOOPBACK  0x7F000001u

#define IPPROTO_UDP      17

static inline uint16_t htons(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}

static inline uint32_t htonl(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

static inline uint32_t ntohl(uint32_t x) {
    return htonl(x);
}
