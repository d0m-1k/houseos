#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define LINE_CAP 64

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static char *trim(char *s) {
    uint32_t len;
    if (!s) return s;
    while (*s && is_space(*s)) s++;
    len = (uint32_t)strlen(s);
    while (len > 0 && is_space(s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    return s;
}

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

int main(void) {
    char line[LINE_CAP];
    uint32_t seed = get_ticks() ^ ((uint32_t)getpid() << 16);
    int target = (int)(seed % 100u) + 1;
    int tries = 0;

    printf("tui_guess: guess number 1..100 (type quit)\n");

    while (1) {
        int v;
        char *t;
        printf("guess> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        t = trim(line);
        if (t[0] == '\0') continue;
        if (strcmp(t, "quit") == 0 || strcmp(t, "exit") == 0) break;
        if (parse_i32(t, &v) != 0) {
            printf("enter integer\n");
            continue;
        }
        tries++;
        if (v < target) printf("higher\n");
        else if (v > target) printf("lower\n");
        else {
            printf("correct in %d tries\n", tries);
            break;
        }
    }

    return 0;
}
