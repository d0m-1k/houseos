#include <stdio.h>

__attribute__((section(".interp"), used))
static const char g_interp_path[] = "/lib/ld-house.so";

int main(int argc, char **argv) {
    printf("dynhello: argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]=%s\n", i, argv[i] ? argv[i] : "(null)");
    }
    return 0;
}
