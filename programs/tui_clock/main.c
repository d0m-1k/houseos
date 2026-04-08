#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>

static int parse_i32(const char *s, int *out) {
    int sign = 1;
    int val = 0;
    uint32_t i = 0;
    if (!s || !out || s[0] == '\0') return -1;
    if (s[0] == '-') {
        sign = -1;
        i = 1;
        if (s[1] == '\0') return -1;
    } else if (s[0] == '+') {
        i = 1;
        if (s[1] == '\0') return -1;
    }
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        val = val * 10 + (s[i] - '0');
    }
    *out = val * sign;
    return 0;
}

int main(int argc, char **argv) {
    int seconds = 15;
    int i;

    if (argc >= 2) {
        int v = 0;
        if (parse_i32(argv[1], &v) == 0 && v > 0 && v <= 3600) seconds = v;
    }

    printf("tui_clock: updates=%d\n", seconds);
    for (i = 0; i < seconds; i++) {
        uint32_t ticks = get_ticks();
        uint32_t total = ticks / 100u;
        uint32_t h = total / 3600u;
        uint32_t m = (total % 3600u) / 60u;
        uint32_t s = total % 60u;
        printf("uptime %u:%u:%u (ticks=%u)\n", h, m, s, ticks);
        usleep(1000000u);
    }
    return 0;
}
