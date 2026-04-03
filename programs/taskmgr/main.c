#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>

static uint32_t logical_cores_count(void) {
    uint32_t eax = 1;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    __asm__ __volatile__("cpuid"
        : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx)
        :
        : "memory");
    if (((ebx >> 16) & 0xFFu) == 0u) return 1u;
    return (ebx >> 16) & 0xFFu;
}

int main(void) {
    char cmd[32];
    while (1) {
        uint32_t ticks = get_ticks();
        uint32_t sec = ticks / 1000u;
        uint32_t h = sec / 3600u;
        uint32_t m = (sec % 3600u) / 60u;
        uint32_t s = sec % 60u;

        printf("\n");
        printf("+----------------------------------+\n");
        printf("| HouseOS Task Manager             |\n");
        printf("+----------------------------------+\n");
        printf("| Uptime        : %u:%u:%u\n", h, m, s);
        printf("| Logical cores : %u\n", logical_cores_count());
        printf("| Tick counter  : %u\n", ticks);
        printf("+----------------------------------+\n");
        printf("| Commands: refresh | exit         |\n");
        printf("+----------------------------------+\n");
        printf("taskmgr> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;
        if (cmd[0] == '\n' || cmd[0] == '\r' || strcmp(cmd, "refresh\n") == 0 || strcmp(cmd, "refresh\r\n") == 0) {
            continue;
        }
        if (strcmp(cmd, "exit\n") == 0 || strcmp(cmd, "exit\r\n") == 0) break;
        printf("Unknown command: %s", cmd);
    }
    return 0;
}
