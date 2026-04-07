#include "commands.h"
#include "cmd_common.h"

#include <syscall.h>
#include <devctl.h>
#include <stdio.h>
#include <string.h>

int cmd_vesa(int argc, char **argv, int arg0, const char *cwd) {
    int fd;
    dev_bootloader_set_t req;
    mode_filter_t mf;
    (void)cwd;

    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: vesa modes [w=] [h=] [b=] [r=] | vesa info | vesa get | vesa set <mode> | vesa rotation [get|set <0|90|180|270>]\n");
        return 1;
    }

    if (strcmp(argv[arg0 + 1], "rotation") == 0) {
        int fdev = open("/dev/vesa", 0);
        uint32_t rot = 0;
        if (fdev < 0) {
            fprintf(stderr, "vesa: cannot open /dev/vesa\n");
            return 1;
        }
        if (arg0 + 2 >= argc || strcmp(argv[arg0 + 2], "get") == 0) {
            if (ioctl(fdev, DEV_IOCTL_VESA_GET_ROTATION, &rot) != 0) {
                close(fdev);
                fprintf(stderr, "vesa: rotation get failed\n");
                return 1;
            }
            fprintf(stdout, "%u\n", rot);
            close(fdev);
            return 0;
        }
        if (strcmp(argv[arg0 + 2], "set") == 0 && arg0 + 3 < argc && parse_u32(argv[arg0 + 3], &rot) == 0) {
            if (ioctl(fdev, DEV_IOCTL_VESA_SET_ROTATION, &rot) != 0) {
                close(fdev);
                fprintf(stderr, "vesa: rotation set failed\n");
                return 1;
            }
            close(fdev);
            return 0;
        }
        close(fdev);
        fprintf(stderr, "usage: vesa rotation [get|set <0|90|180|270>]\n");
        return 1;
    }

    fd = open("/dev/bootloader", 0);
    if (fd < 0) {
        fprintf(stderr, "vesa: cannot open /dev/bootloader\n");
        return 1;
    }

    if (strcmp(argv[arg0 + 1], "modes") == 0) {
        dev_bootloader_modes_t modes;
        if (parse_modes_filters(argc, argv, arg0 + 2, &mf) != 0) {
            close(fd);
            fprintf(stderr, "usage: vesa modes [w=] [h=] [b=] [r=]\n");
            return 1;
        }
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_GET_MODES, &modes) != 0) {
            close(fd);
            fprintf(stderr, "vesa: modes query failed\n");
            return 1;
        }
        for (uint32_t i = 0; i < modes.count; i++) {
            dev_bootloader_mode_t *m = &modes.modes[i];
            if (m->width == 0 || m->height == 0 || m->bpp == 0) continue;
            if (!mode_matches(&mf, m->width, m->height, m->bpp)) continue;
            print_mode_line(m->id, m->width, m->height, m->bpp);
        }
        close(fd);
        return 0;
    }

    if (strcmp(argv[arg0 + 1], "info") == 0) {
        dev_fb_info_t fi;
        int fdev = open("/dev/vesa", 0);
        if (fdev < 0) {
            close(fd);
            fprintf(stderr, "vesa: cannot open /dev/vesa\n");
            return 1;
        }
        if (ioctl(fdev, DEV_IOCTL_VESA_GET_INFO, &fi) != 0) {
            close(fdev);
            close(fd);
            fprintf(stderr, "vesa: info query failed\n");
            return 1;
        }
        {
            uint32_t g = gcd_u32(fi.width, fi.height);
            uint32_t arw = fi.width / g;
            uint32_t arh = fi.height / g;
            fprintf(stdout, "vesa %ux%u %u:%u bpp=%u pitch=%u addr=0x%x size=%u\n",
                fi.width, fi.height, arw, arh, fi.bpp, fi.pitch, fi.address, fi.size);
        }
        close(fdev);
        close(fd);
        return 0;
    }

    memset(&req, 0, sizeof(req));
    strcpy(req.prop_name, "vesa_mode");
    if (strcmp(argv[arg0 + 1], "get") == 0) {
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_GET, &req) != 0) {
            close(fd);
            fprintf(stderr, "vesa: get failed\n");
            return 1;
        }
        fprintf(stdout, "%u (0x%x)\n", req.value, req.value);
        close(fd);
        return 0;
    }

    if (strcmp(argv[arg0 + 1], "set") == 0 && arg0 + 2 < argc && parse_u32(argv[arg0 + 2], &req.value) == 0) {
        dev_bootloader_set_t sel;
        memset(&sel, 0, sizeof(sel));
        strcpy(sel.prop_name, "video_output");
        sel.value = 0;
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_SET, &sel) != 0) {
            close(fd);
            fprintf(stderr, "vesa: set video_output failed\n");
            return 1;
        }
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_SET, &req) != 0) {
            close(fd);
            fprintf(stderr, "vesa: set mode failed\n");
            return 1;
        }
        close(fd);
        return 0;
    }

    close(fd);
    fprintf(stderr, "usage: vesa modes [w=] [h=] [b=] [r=] | vesa info | vesa get | vesa set <mode> | vesa rotation [get|set <0|90|180|270>]\n");
    return 1;
}
