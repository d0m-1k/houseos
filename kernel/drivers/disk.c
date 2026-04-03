#include <drivers/disk.h>
#include <asm/port.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <devctl.h>
#include <stdint.h>
#include <string.h>

#define ATA_IO_BASE 0x1F0
#define ATA_CTRL_BASE 0x3F6

#define ATA_REG_DATA       0
#define ATA_REG_SECCNT     2
#define ATA_REG_LBA0       3
#define ATA_REG_LBA1       4
#define ATA_REG_LBA2       5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

static uint32_t g_total_sectors = 0;

typedef struct {
    uint32_t base_lba;
    uint32_t total_sectors;
    uint32_t flags;
} disk_part_ctx_t;

static disk_part_ctx_t g_disk_parts[5];

static inline uint8_t ata_status(void) {
    return inb(ATA_IO_BASE + ATA_REG_STATUS);
}

static void ata_400ns_delay(void) {
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
}

static int ata_wait_not_busy(void) {
    uint32_t timeout = 1000000;
    while (timeout--) {
        if (!(ata_status() & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint8_t st = ata_status();
        if (st & ATA_SR_ERR) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static int ata_identify(void) {
    uint16_t id[256];

    if (ata_wait_not_busy() != 0) return -1;

    outb(ATA_IO_BASE + ATA_REG_DRIVE, 0xA0);
    outb(ATA_IO_BASE + ATA_REG_SECCNT, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA1, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA2, 0);
    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (ata_status() == 0) return -1;
    if (ata_wait_drq() != 0) return -1;

    for (uint32_t i = 0; i < 256; i++) id[i] = inw(ATA_IO_BASE + ATA_REG_DATA);

    g_total_sectors = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
    if (g_total_sectors == 0) g_total_sectors = 2880;
    return 0;
}

static int ata_rw28(uint32_t lba, uint32_t count, void *buffer, int write_mode) {
    uint16_t *w = (uint16_t*)buffer;
    if (!buffer || count == 0 || count > 255 || lba > 0x0FFFFFFF) return -1;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        if (ata_wait_not_busy() != 0) return -1;

        outb(ATA_IO_BASE + ATA_REG_DRIVE, (uint8_t)(0xE0 | ((cur >> 24) & 0x0F)));
        outb(ATA_IO_BASE + ATA_REG_SECCNT, 1);
        outb(ATA_IO_BASE + ATA_REG_LBA0, (uint8_t)(cur & 0xFF));
        outb(ATA_IO_BASE + ATA_REG_LBA1, (uint8_t)((cur >> 8) & 0xFF));
        outb(ATA_IO_BASE + ATA_REG_LBA2, (uint8_t)((cur >> 16) & 0xFF));
        outb(ATA_IO_BASE + ATA_REG_COMMAND, write_mode ? ATA_CMD_WRITE_SECTORS : ATA_CMD_READ_SECTORS);

        if (ata_wait_drq() != 0) return -1;

        if (!write_mode) {
            for (uint32_t i = 0; i < 256; i++) w[s * 256 + i] = inw(ATA_IO_BASE + ATA_REG_DATA);
        } else {
            for (uint32_t i = 0; i < 256; i++) outw(ATA_IO_BASE + ATA_REG_DATA, w[s * 256 + i]);
            ata_400ns_delay();
        }
    }

    if (write_mode) {
        if (ata_wait_not_busy() != 0) return -1;
        outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (ata_wait_not_busy() != 0) return -1;
    }
    return 0;
}

static uint32_t rd_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void disk_parts_init(void) {
    uint8_t mbr[512];
    memset(g_disk_parts, 0, sizeof(g_disk_parts));

    g_disk_parts[0].base_lba = 0;
    g_disk_parts[0].total_sectors = g_total_sectors;
    g_disk_parts[0].flags = 1;

    if (ata_rw28(0, 1, mbr, 0) != 0) return;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;

    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t *e = &mbr[446 + i * 16];
        uint32_t start = rd_le32(e + 8);
        uint32_t count = rd_le32(e + 12);
        uint32_t end;
        uint32_t idx = i + 1;

        if (count == 0) continue;
        end = start + count;
        if (end < start || end > g_total_sectors) continue;

        g_disk_parts[idx].base_lba = start;
        g_disk_parts[idx].total_sectors = count;
        g_disk_parts[idx].flags = 1;
    }
}

int disk_read_kernel(uint32_t lba, uint32_t count, void *buffer) {
    uint32_t end_lba;
    if (!buffer || count == 0 || count > 255 || g_total_sectors == 0) return -1;
    if (lba >= g_total_sectors) return -1;
    end_lba = lba + count;
    if (end_lba < lba || end_lba > g_total_sectors) return -1;
    return ata_rw28(lba, count, buffer, 0);
}

int disk_write_kernel(uint32_t lba, uint32_t count, const void *buffer) {
    uint32_t end_lba;
    if (!buffer || count == 0 || count > 255 || g_total_sectors == 0) return -1;
    if (lba >= g_total_sectors) return -1;
    end_lba = lba + count;
    if (end_lba < lba || end_lba > g_total_sectors) return -1;
    return ata_rw28(lba, count, (void*)buffer, 1);
}

int disk_get_partition_info(uint32_t index, uint32_t *start_lba, uint32_t *total_sectors) {
    if (index > 4 || !start_lba || !total_sectors) return -1;
    *start_lba = g_disk_parts[index].base_lba;
    *total_sectors = g_disk_parts[index].total_sectors;
    if (*total_sectors == 0) return -1;
    return 0;
}

static int disk_ioctl(void *ctx, uint32_t request, void *arg) {
    disk_part_ctx_t *part = (disk_part_ctx_t*)ctx;
    if (!part) return -1;
    if (request == DEV_IOCTL_DISK_GET_INFO) {
        dev_disk_info_t *out = (dev_disk_info_t*)arg;
        if (!out) return -1;
        out->sector_size = 512;
        out->total_sectors = part->total_sectors;
        out->flags = part->flags;
        return 0;
    }
    if (request == DEV_IOCTL_DISK_READ || request == DEV_IOCTL_DISK_WRITE) {
        dev_disk_rw_t *rw = (dev_disk_rw_t*)arg;
        uint32_t bytes;
        uint32_t end_lba;
        if (!rw || rw->count == 0 || rw->buffer == 0 || part->total_sectors == 0) return -1;
        if (rw->count > 255) return -1;
        if (rw->lba >= part->total_sectors) return -1;
        end_lba = rw->lba + rw->count;
        if (end_lba < rw->lba || end_lba > part->total_sectors) return -1;
        bytes = rw->count * 512u;
        if (bytes / 512u != rw->count) return -1;
        if (rw->buffer + bytes < rw->buffer) return -1;
        return ata_rw28(part->base_lba + rw->lba, rw->count, (void*)(uintptr_t)rw->buffer, request == DEV_IOCTL_DISK_WRITE);
    }
    return -1;
}

void disk_init(devfs_t *devfs) {
    if (!devfs) return;
    if (ata_identify() != 0) return;
    disk_parts_init();
    devfs_create_dir(devfs, "/disk");
    devfs_create_device_ops(devfs, "/disk/0", 0, 0, 0, disk_ioctl, &g_disk_parts[0]);
    for (uint32_t i = 1; i < 5; i++) {
        char path[] = "/disk/0p0";
        if (g_disk_parts[i].total_sectors == 0) continue;
        path[8] = (char)('0' + i);
        devfs_create_device_ops(devfs, path, 0, 0, 0, disk_ioctl, &g_disk_parts[i]);
    }
}
