#include <drivers/pci.h>
#include <asm/port.h>
#include <devctl.h>
#include <string.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_INVALID_VENDOR 0xFFFF

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static uint32_t g_pci_device_count = 0;
static const pci_device_t *g_pci_dev_nodes[PCI_MAX_DEVICES];

static uint32_t pci_cfg_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (1u << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) |
           (offset & 0xFCu);
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outdw(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, slot, func, offset));
    return indw(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outdw(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, slot, func, offset));
    outdw(PCI_CONFIG_DATA, value);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset);
    return (uint16_t)((v >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

void pci_cfg_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t aligned = offset & 0xFCu;
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t oldv = pci_read32(bus, slot, func, (uint8_t)aligned);
    uint32_t newv = (oldv & ~mask) | (((uint32_t)value << shift) & mask);
    pci_write32(bus, slot, func, (uint8_t)aligned, newv);
}

static uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset);
    return (uint8_t)((v >> ((offset & 3u) * 8u)) & 0xFFu);
}

static void pci_add_device(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *d;
    uint8_t i;

    if (g_pci_device_count >= PCI_MAX_DEVICES) return;
    d = &g_pci_devices[g_pci_device_count];
    memset(d, 0, sizeof(*d));

    d->bus = bus;
    d->slot = slot;
    d->func = func;
    d->vendor_id = pci_cfg_read16(bus, slot, func, 0x00);
    d->device_id = pci_cfg_read16(bus, slot, func, 0x02);
    d->revision = pci_read8(bus, slot, func, 0x08);
    d->prog_if = pci_read8(bus, slot, func, 0x09);
    d->subclass = pci_read8(bus, slot, func, 0x0A);
    d->class_code = pci_read8(bus, slot, func, 0x0B);
    d->header_type = (uint8_t)(pci_read8(bus, slot, func, 0x0E) & 0x7Fu);
    d->irq_line = pci_read8(bus, slot, func, 0x3C);

    if (d->header_type == 0x00) {
        for (i = 0; i < 6; i++) d->bar[i] = pci_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
    }

    g_pci_device_count++;
}

void pci_init(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor0 = pci_cfg_read16((uint8_t)bus, slot, 0, 0x00);
            uint8_t header;
            uint8_t max_func;

            if (vendor0 == PCI_INVALID_VENDOR) continue;
            header = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            max_func = (header & 0x80u) ? 8u : 1u;

            for (uint8_t func = 0; func < max_func; func++) {
                uint16_t vendor = pci_cfg_read16((uint8_t)bus, slot, func, 0x00);
                if (vendor == PCI_INVALID_VENDOR) continue;
                pci_add_device((uint8_t)bus, slot, func);
            }
        }
    }
}

uint32_t pci_device_count(void) {
    return g_pci_device_count;
}

const pci_device_t *pci_get_device(uint32_t index) {
    if (index >= g_pci_device_count) return NULL;
    return &g_pci_devices[index];
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, uint32_t occurrence) {
    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        const pci_device_t *d = &g_pci_devices[i];
        if (d->class_code != class_code) continue;
        if (d->subclass != subclass) continue;
        if (prog_if != 0xFFu && d->prog_if != prog_if) continue;
        if (occurrence == 0) return d;
        occurrence--;
    }
    return NULL;
}

static void hex2(char *out, uint8_t v) {
    static const char h[] = "0123456789abcdef";
    out[0] = h[(v >> 4) & 0xF];
    out[1] = h[v & 0xF];
}

static int pci_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    const pci_device_t *d = (const pci_device_t*)ctx;
    if (!d || !arg) return -1;

    if (request == DEV_IOCTL_PCI_GET_INFO) {
        dev_pci_info_t *out = (dev_pci_info_t*)arg;
        out->bus = d->bus;
        out->slot = d->slot;
        out->func = d->func;
        out->vendor_id = d->vendor_id;
        out->device_id = d->device_id;
        out->class_code = d->class_code;
        out->subclass = d->subclass;
        out->prog_if = d->prog_if;
        out->revision = d->revision;
        out->header_type = d->header_type;
        out->irq_line = d->irq_line;
        for (uint32_t i = 0; i < 6; i++) out->bar[i] = d->bar[i];
        return 0;
    }

    if (request == DEV_IOCTL_PCI_CFG_READ32) {
        dev_pci_cfg_rw_t *rw = (dev_pci_cfg_rw_t*)arg;
        if ((rw->offset & 3u) != 0u || rw->offset > 0xFCu) return -1;
        rw->value = pci_read32(d->bus, d->slot, d->func, (uint8_t)rw->offset);
        return 0;
    }

    return -1;
}

int pci_devfs_init(devfs_t *devfs) {
    if (!devfs || !devfs->fs) return -1;
    if (devfs_create_dir(devfs, "/bus") != 0) return -1;
    if (devfs_create_dir(devfs, "/bus/pci") != 0) return -1;

    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        char path_pci[] = "/bus/pci/00.00.0";
        const pci_device_t *d = &g_pci_devices[i];
        g_pci_dev_nodes[i] = d;
        hex2(&path_pci[9], d->bus);
        hex2(&path_pci[12], d->slot);
        path_pci[15] = (char)('0' + (d->func & 0x7));
        if (devfs_create_device_ops(devfs, path_pci, 0, NULL, NULL, pci_dev_ioctl, (void*)g_pci_dev_nodes[i]) != 0) {
            return -1;
        }
    }

    return 0;
}
