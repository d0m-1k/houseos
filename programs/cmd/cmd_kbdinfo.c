#include "commands.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>

int cmd_kbdinfo(int argc, char **argv, int arg0, const char *cwd) {
    dev_keyboard_info_t ki;
    int fd;
    (void)argc; (void)argv; (void)arg0; (void)cwd;
    fd = open("/dev/keyboard", 0);
    if (fd < 0 || ioctl(fd, DEV_IOCTL_KBD_GET_INFO, &ki) != 0) return 1;
    fprintf(stdout, "kbd layout=%u caps=%u num=%u scroll=%u\n",
        ki.layout, ki.caps_lock, ki.num_lock, ki.scroll_lock);
    close(fd);
    return 0;
}
