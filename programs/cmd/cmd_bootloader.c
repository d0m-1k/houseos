#include "commands.h"
#include "cmd_common.h"

#include <syscall.h>
#include <devctl.h>
#include <stdio.h>
#include <string.h>

int cmd_bootloader(int argc, char **argv, int arg0, const char *cwd) {
    int fd;
    dev_bootloader_set_t req;
    const char *sub;
    const char *prop;
    (void)cwd;

    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: bootloader set <prop_name> <value> | bootloader set root <auto|disk0> | bootloader get <prop_name>\n");
        return 1;
    }
    sub = argv[arg0 + 1];
    fd = open("/dev/bootloader", 0);
    if (fd < 0) {
        fprintf(stderr, "bootloader: cannot open /dev/bootloader\n");
        return 1;
    }
    if (arg0 + 2 >= argc) {
        close(fd);
        fprintf(stderr, "usage: bootloader set <prop_name> <value> | bootloader set root <auto|disk0> | bootloader get <prop_name>\n");
        return 1;
    }
    prop = argv[arg0 + 2];
    if (strlen(prop) >= sizeof(req.prop_name)) {
        close(fd);
        fprintf(stderr, "prop_name too long\n");
        return 1;
    }
    memset(&req, 0, sizeof(req));
    strcpy(req.prop_name, prop);

    if (strcmp(sub, "set") == 0) {
        if (strcmp(prop, "root") == 0 || strcmp(prop, "root_disk") == 0) {
            if (arg0 + 3 >= argc) {
                close(fd);
                fprintf(stderr, "bad root disk\n");
                return 1;
            }
            if (strcmp(argv[arg0 + 3], "auto") == 0) req.value = 0;
            else if (strcmp(argv[arg0 + 3], "0") == 0 || strcmp(argv[arg0 + 3], "disk0") == 0) req.value = 1;
            else {
                close(fd);
                fprintf(stderr, "bad root disk\n");
                return 1;
            }
            strcpy(req.prop_name, "root_disk");
        } else if (arg0 + 3 >= argc || parse_u32(argv[arg0 + 3], &req.value) != 0) {
            close(fd);
            fprintf(stderr, "bad value\n");
            return 1;
        }
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_SET, &req) != 0) {
            close(fd);
            fprintf(stderr, "bootloader: set failed\n");
            return 1;
        }
        close(fd);
        return 0;
    }

    if (strcmp(sub, "get") == 0) {
        if (strcmp(prop, "root") == 0) strcpy(req.prop_name, "root_disk");
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_GET, &req) != 0) {
            close(fd);
            fprintf(stderr, "bootloader: get failed\n");
            return 1;
        }
        if (strcmp(req.prop_name, "root_disk") == 0) {
            const char *name = "auto";
            if (req.value == 1) name = "disk0";
            fprintf(stdout, "root=%s (%u)\n", name, req.value);
        } else {
            fprintf(stdout, "%s=%u (0x%x)\n", req.prop_name, req.value, req.value);
        }
        close(fd);
        return 0;
    }

    fprintf(stderr, "usage: bootloader set <prop_name> <value> | bootloader set root <auto|disk0> | bootloader get <prop_name>\n");
    close(fd);
    return 1;
}
