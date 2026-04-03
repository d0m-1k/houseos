#include "commands.h"
#include "cmd_common.h"

#include <syscall.h>
#include <devctl.h>
#include <stdio.h>
#include <string.h>

int cmd_vga(int argc, char **argv, int arg0, const char *cwd) {
    int fd;
    dev_bootloader_set_t req;
    mode_filter_t mf;
    (void)cwd;

    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: vga modes [w=] [h=] [b=] [r=] | vga info | vga get | vga set <mode>\n");
        return 1;
    }
    fd = open("/dev/bootloader", 0);
    if (fd < 0) return 1;

    if (strcmp(argv[arg0 + 1], "modes") == 0) {
        if (parse_modes_filters(argc, argv, arg0 + 2, &mf) != 0) {
            close(fd);
            fprintf(stderr, "usage: vga modes [w=] [h=] [b=] [r=]\n");
            return 1;
        }
        for (uint32_t i = 0; i < g_vga_mode_desc_count; i++) {
            const mode_desc_t *d = &g_vga_mode_desc[i];
            if (!mode_matches(&mf, d->width, d->height, d->bpp)) continue;
            print_mode_line(d->id, d->width, d->height, d->bpp);
        }
        close(fd);
        return 0;
    }

    if (strcmp(argv[arg0 + 1], "info") == 0) {
        dev_vga_info_t vi;
        int vdev = open("/dev/vga", 0);
        if (vdev < 0) {
            close(fd);
            return 1;
        }
        if (ioctl(vdev, DEV_IOCTL_VGA_GET_INFO, &vi) != 0) {
            close(vdev);
            close(fd);
            return 1;
        }
        {
            uint32_t g = gcd_u32(vi.cols, vi.rows);
            uint32_t arw = vi.cols / g;
            uint32_t arh = vi.rows / g;
            fprintf(stdout, "vga %ux%u %u:%u mode=0x%x (%u) addr=0x%x size=%u\n",
                vi.cols, vi.rows, arw, arh, vi.mode, vi.mode, vi.address, vi.size);
        }
        close(vdev);
        close(fd);
        return 0;
    }

    memset(&req, 0, sizeof(req));
    strcpy(req.prop_name, "vga_mode");
    if (strcmp(argv[arg0 + 1], "get") == 0) {
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_GET, &req) != 0) {
            close(fd);
            return 1;
        }
        fprintf(stdout, "%u (0x%x)\n", req.value, req.value);
        close(fd);
        return 0;
    }

    if (strcmp(argv[arg0 + 1], "set") == 0 && arg0 + 2 < argc && parse_u32(argv[arg0 + 2], &req.value) == 0) {
        dev_bootloader_set_t sel;
        if (!is_supported_vga_mode(req.value)) {
            close(fd);
            fprintf(stderr, "supported modes: 0, 2, 3\n");
            return 1;
        }
        memset(&sel, 0, sizeof(sel));
        strcpy(sel.prop_name, "video_output");
        sel.value = 1;
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_SET, &sel) != 0) {
            close(fd);
            return 1;
        }
        if (ioctl(fd, DEV_IOCTL_BOOTLOADER_SET, &req) != 0) {
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }

    close(fd);
    fprintf(stderr, "usage: vga modes [w=] [h=] [b=] [r=] | vga info | vga get | vga set <mode>\n");
    return 1;
}
