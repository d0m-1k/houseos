#include <drivers/bootloader.h>
#include <drivers/disk.h>
#include <devctl.h>
#include <string.h>

#define BOOTCFG_ADDR 0x00000600u
#define CFG_MAGIC    0x47464348u 

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t kernel_size;
    uint32_t kernel_lba;
    uint32_t kernel_addr;
    uint32_t initramfs_size;
    uint32_t initramfs_lba;
    uint32_t initramfs_addr;
    uint32_t memmap_addr;
    uint16_t video_output;
    uint16_t vesa_mode;
    uint16_t vga_mode;
    uint16_t reserved0;
    uint32_t vesa_info_addr;
    uint32_t vesa_mode_info_addr;
    uint32_t vesa_modes_addr;
    uint32_t stage2_lba;
    uint32_t stage2_sectors;
    uint32_t flags;
    uint32_t rootfs_lba;
    uint32_t rootfs_size;
    uint32_t root_disk;
} bootcfg_t;

static bootcfg_t g_cfg;

static int bootloader_cfg_flush(void) {
    uint8_t sector_buf[1024];
    if (disk_read_kernel(1, 2, sector_buf) != 0) return -1;
    memcpy(sector_buf, &g_cfg, sizeof(g_cfg));
    return disk_write_kernel(1, 2, sector_buf);
}

static int bootloader_set_prop(const char *name, uint32_t value) {
    if (!name || !name[0]) return -1;

    if (strcmp(name, "kernel_size") == 0) g_cfg.kernel_size = value;
    else if (strcmp(name, "kernel_lba") == 0) g_cfg.kernel_lba = value;
    else if (strcmp(name, "kernel_addr") == 0) g_cfg.kernel_addr = value;
    else if (strcmp(name, "initramfs_size") == 0) g_cfg.initramfs_size = value;
    else if (strcmp(name, "initramfs_lba") == 0) g_cfg.initramfs_lba = value;
    else if (strcmp(name, "initramfs_addr") == 0) g_cfg.initramfs_addr = value;
    else if (strcmp(name, "memmap_addr") == 0) g_cfg.memmap_addr = value;
    else if (strcmp(name, "video_output") == 0) g_cfg.video_output = (uint16_t)(value & 0xFFFFu);
    else if (strcmp(name, "vesa_mode") == 0) g_cfg.vesa_mode = (uint16_t)(value & 0xFFFFu);
    else if (strcmp(name, "vga_mode") == 0) g_cfg.vga_mode = (uint16_t)(value & 0xFFFFu);
    else if (strcmp(name, "vesa_info_addr") == 0) g_cfg.vesa_info_addr = value;
    else if (strcmp(name, "vesa_mode_info_addr") == 0) g_cfg.vesa_mode_info_addr = value;
    else if (strcmp(name, "vesa_modes_addr") == 0) g_cfg.vesa_modes_addr = value;
    else if (strcmp(name, "stage2_lba") == 0) g_cfg.stage2_lba = value;
    else if (strcmp(name, "stage2_sectors") == 0) g_cfg.stage2_sectors = value;
    else if (strcmp(name, "flags") == 0) g_cfg.flags = value;
    else if (strcmp(name, "rootfs_lba") == 0) g_cfg.rootfs_lba = value;
    else if (strcmp(name, "rootfs_size") == 0) g_cfg.rootfs_size = value;
    else if (strcmp(name, "root_disk") == 0 || strcmp(name, "root") == 0) g_cfg.root_disk = value;
    else return -1;

    return 0;
}

static int bootloader_get_prop(const char *name, uint32_t *out) {
    if (!name || !name[0] || !out) return -1;

    if (strcmp(name, "kernel_size") == 0) *out = g_cfg.kernel_size;
    else if (strcmp(name, "kernel_lba") == 0) *out = g_cfg.kernel_lba;
    else if (strcmp(name, "kernel_addr") == 0) *out = g_cfg.kernel_addr;
    else if (strcmp(name, "initramfs_size") == 0) *out = g_cfg.initramfs_size;
    else if (strcmp(name, "initramfs_lba") == 0) *out = g_cfg.initramfs_lba;
    else if (strcmp(name, "initramfs_addr") == 0) *out = g_cfg.initramfs_addr;
    else if (strcmp(name, "memmap_addr") == 0) *out = g_cfg.memmap_addr;
    else if (strcmp(name, "video_output") == 0) *out = (uint32_t)g_cfg.video_output;
    else if (strcmp(name, "vesa_mode") == 0) *out = (uint32_t)g_cfg.vesa_mode;
    else if (strcmp(name, "vga_mode") == 0) *out = (uint32_t)g_cfg.vga_mode;
    else if (strcmp(name, "vesa_info_addr") == 0) *out = g_cfg.vesa_info_addr;
    else if (strcmp(name, "vesa_mode_info_addr") == 0) *out = g_cfg.vesa_mode_info_addr;
    else if (strcmp(name, "vesa_modes_addr") == 0) *out = g_cfg.vesa_modes_addr;
    else if (strcmp(name, "stage2_lba") == 0) *out = g_cfg.stage2_lba;
    else if (strcmp(name, "stage2_sectors") == 0) *out = g_cfg.stage2_sectors;
    else if (strcmp(name, "flags") == 0) *out = g_cfg.flags;
    else if (strcmp(name, "rootfs_lba") == 0) *out = g_cfg.rootfs_lba;
    else if (strcmp(name, "rootfs_size") == 0) *out = g_cfg.rootfs_size;
    else if (strcmp(name, "root_disk") == 0 || strcmp(name, "root") == 0) *out = g_cfg.root_disk;
    else return -1;

    return 0;
}

static int bootloader_get_modes(dev_bootloader_modes_t *out) {
    const uint8_t *src;
    uint32_t count;
    if (!out) return -1;
    out->count = 0;
    if (g_cfg.vesa_modes_addr == 0) return -1;
    src = (const uint8_t*)(uintptr_t)g_cfg.vesa_modes_addr;
    count = *(const uint32_t*)(const void*)src;
    if (count > 256) count = 256;
    out->count = count;
    memcpy(out->modes, src + 4, count * sizeof(dev_bootloader_mode_t));
    return (count > 0) ? 0 : -1;
}

static int bootloader_ioctl(void *ctx, uint32_t request, void *arg) {
    dev_bootloader_set_t *in;
    dev_bootloader_modes_t *modes;
    (void)ctx;
    if (!arg) return -1;

    in = (dev_bootloader_set_t*)arg;
    modes = (dev_bootloader_modes_t*)arg;
    if (request == DEV_IOCTL_BOOTLOADER_SET) {
        if (bootloader_set_prop(in->prop_name, in->value) != 0) return -1;
        return bootloader_cfg_flush();
    }
    if (request == DEV_IOCTL_BOOTLOADER_GET) return bootloader_get_prop(in->prop_name, &in->value);
    if (request == DEV_IOCTL_BOOTLOADER_GET_MODES) return bootloader_get_modes(modes);
    return -1;
}

int bootloader_dev_init(devfs_t *devfs) {
    bootcfg_t cfg_src;
    if (!devfs || !devfs->fs) return -1;

    memset(&cfg_src, 0, sizeof(cfg_src));
    memcpy(&cfg_src, (const void*)(uintptr_t)BOOTCFG_ADDR, sizeof(cfg_src));

    if (cfg_src.magic == CFG_MAGIC) memcpy(&g_cfg, &cfg_src, sizeof(g_cfg));
    else memset(&g_cfg, 0, sizeof(g_cfg));

    if (devfs_create_device_ops(devfs, "/bootloader", 0, NULL, NULL, bootloader_ioctl, NULL) != 0) {
        return -1;
    }
    return 0;
}

uint32_t bootloader_get_root_disk(void) {
    return g_cfg.root_disk;
}

uint32_t bootloader_get_flags(void) {
    return g_cfg.flags;
}
