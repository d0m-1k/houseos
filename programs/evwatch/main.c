#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <devctl.h>

int main(int argc, char **argv) {
    const char *path = "/dev/keyboard";
    int fd;
    dev_keyboard_event_t ev;

    if (argc >= 2 && argv[1] && argv[1][0]) path = argv[1];

    fd = open(path, 0);
    if (fd < 0) {
        fprintf(stderr, "evwatch: open failed: %s\n", path);
        return 1;
    }

    printf("evwatch: reading %s\n", path);
    while (1) {
        int32_t n = read(fd, &ev, sizeof(ev));
        if (n != (int32_t)sizeof(ev)) continue;
        printf("%s sc=%u", ev.pressed ? "DOWN" : "UP", (uint32_t)ev.scancode);
        if (ev.ascii >= 32 && ev.ascii <= 126) printf(" ascii='%c'", (char)ev.ascii);
        else if (ev.ascii == '\n') printf(" ascii='\\n'");
        else if (ev.ascii == '\t') printf(" ascii='\\t'");
        else if (ev.ascii) printf(" ascii=%d", (int)ev.ascii);
        printf(" mod[s=%u c=%u a=%u caps=%u]\n",
               (uint32_t)ev.shift, (uint32_t)ev.ctrl, (uint32_t)ev.alt, (uint32_t)ev.caps);
    }

    close(fd);
    return 0;
}
