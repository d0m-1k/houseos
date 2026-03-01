#include <stdio.h>
#include <syscall.h>

int main(void) {
    printf("init: start\n");
    init_spawn_shells();
    return 0;
}
