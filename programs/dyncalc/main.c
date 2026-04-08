#include <stdio.h>

extern int demo_compute(int x);

int main(void) {
    int x = 11;
    int y = demo_compute(x);
    printf("dyncalc: demo_compute(%d)=%d\n", x, y);
    return 0;
}
