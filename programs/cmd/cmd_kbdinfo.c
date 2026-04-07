#include "commands.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>

int cmd_kbdinfo(int argc, char **argv, int arg0, const char *cwd) {
    dev_keyboard_info_t ki;
    int fd;
    (void)cwd;
    if (arg0 + 1 < argc) {
        fprintf(stderr, "usage: kbdinfo\n");
        return 1;
    }
    (void)argv;
    fd = open("/dev/keyboard", 0);
    if (fd < 0) {
        fprintf(stderr, "kbdinfo: cannot open /dev/keyboard\n");
        return 1;
    }
    if (ioctl(fd, DEV_IOCTL_KBD_GET_INFO, &ki) != 0) {
        close(fd);
        fprintf(stderr, "kbdinfo: ioctl failed\n");
        return 1;
    }
    fprintf(stdout, "kbd layout=%u caps=%u num=%u scroll=%u\n",
        ki.layout, ki.caps_lock, ki.num_lock, ki.scroll_lock);
    close(fd);
    return 0;
}
