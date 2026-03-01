#include <stdlib.h>

void utoa(unsigned int value, char *buf, unsigned int base) {
    char tmp[32];
    unsigned int i = 0;
    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return;
    }
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (value > 0) {
        unsigned int d = value % base;
        tmp[i++] = (d < 10) ? (char)('0' + d) : (char)('a' + (d - 10));
        value /= base;
    }
    for (unsigned int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}
