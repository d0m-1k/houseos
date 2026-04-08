#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE_CAP 128

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

static int parse_expr(char *line, int *a, char *op, int *b) {
    char *p = trim(line);
    uint32_t i = 0;
    char left[32];
    char right[32];
    uint32_t l = 0;
    uint32_t r = 0;

    while (p[i] && is_space(p[i])) i++;
    while (p[i] && !is_space(p[i]) && p[i] != '+' && p[i] != '-' && p[i] != '*' && p[i] != '/' && p[i] != '%') {
        if (l + 1 >= sizeof(left)) return -1;
        left[l++] = p[i++];
    }
    left[l] = '\0';

    while (p[i] && is_space(p[i])) i++;
    if (!p[i]) return -1;
    *op = p[i++];

    while (p[i] && is_space(p[i])) i++;
    while (p[i] && !is_space(p[i])) {
        if (r + 1 >= sizeof(right)) return -1;
        right[r++] = p[i++];
    }
    right[r] = '\0';

    while (p[i]) {
        if (!is_space(p[i])) return -1;
        i++;
    }

    if (parse_i32(left, a) != 0 || parse_i32(right, b) != 0) return -1;
    return 0;
}

int main(void) {
    char line[LINE_CAP];

    printf("tui_calc: enter expressions like 2 + 3\n");
    printf("commands: help, quit\n");

    while (1) {
        int a, b;
        int res;
        char op;
        char *t;

        printf("calc> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        t = trim(line);
        if (t[0] == '\0') continue;
        if (strcmp(t, "quit") == 0 || strcmp(t, "exit") == 0) break;
        if (strcmp(t, "help") == 0) {
            printf("Supported operators: + - * / %%\n");
            continue;
        }
        if (parse_expr(t, &a, &op, &b) != 0) {
            printf("parse error\n");
            continue;
        }

        if (op == '+') res = a + b;
        else if (op == '-') res = a - b;
        else if (op == '*') res = a * b;
        else if (op == '/') {
            if (b == 0) {
                printf("division by zero\n");
                continue;
            }
            res = a / b;
        } else if (op == '%') {
            if (b == 0) {
                printf("mod by zero\n");
                continue;
            }
            res = a % b;
        } else {
            printf("unknown operator\n");
            continue;
        }

        printf("= %d\n", res);
    }

    return 0;
}
