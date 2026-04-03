#pragma once

#include <stdint.h>
#include <drivers/filesystem/devfs.h>

#define PCI_MAX_DEVICES 128

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq_line;
    uint32_t bar[6];
} pci_device_t;

void pci_init(void);
int pci_devfs_init(devfs_t *devfs);
uint32_t pci_device_count(void);
const pci_device_t *pci_get_device(uint32_t index);
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, uint32_t occurrence);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_cfg_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
