#include <syscall.h>
#include <stdint.h>
#include <devctl.h>

static uint8_t fb[320 * 8 * 4];

int main(void) {
    dev_fb_info_t info;
    int bpp_bytes = 0;
    int size = 0;
    int fd = -1;

    fd = open("/devices/framebuffer/buffer", 0);
    if (fd < 0) return 1;
    if (ioctl(fd, DEV_IOCTL_FB_GET_INFO, &info) != 0) return 1;

    bpp_bytes = (int)(info.bpp / 8);
    if (info.pitch == 0 || (bpp_bytes != 3 && bpp_bytes != 4)) return 1;
    size = 320 * 8 * bpp_bytes;

    for (int i = 0; i < size; i += bpp_bytes) {
        fb[i + 0] = 0xFF;
        fb[i + 1] = 0x00;
        fb[i + 2] = 0x00;
        if (bpp_bytes == 4) fb[i + 3] = 0;
    }

    (void)write(fd, fb, (uint32_t)size);
    (void)close(fd);
    return 0;
}
