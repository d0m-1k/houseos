#pragma once

#include <stdint.h>
#include <drivers/filesystem/devfs.h>
#include <devctl.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq_line;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base;
    uint8_t cap_length;
    uint8_t max_ports;
    uint8_t connected_ports;
    uint16_t hci_version;
} xhci_info_t;

typedef struct {
    uint8_t valid;
    uint8_t iface_num;
    uint8_t iface_class;
    uint8_t iface_subclass;
    uint8_t iface_proto;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint8_t intr_in_ep;
    uint8_t intr_interval;
    uint16_t bulk_mps;
    uint16_t intr_mps;
} xhci_iface_info_t;

void xhci_init(devfs_t *devfs);
uint32_t xhci_count(void);
const xhci_info_t *xhci_get(uint32_t index);
int xhci_get_dev_state(uint32_t hc_index, uint32_t port, dev_usb_dev_info_t *out);
int xhci_get_iface_info(uint32_t hc_index, uint32_t port, xhci_iface_info_t *out);
void xhci_rescan_all(void);
int xhci_configure_bulk_endpoints(
    uint32_t hc_index, uint32_t port, uint8_t bulk_in_ep, uint8_t bulk_out_ep, uint16_t bulk_mps
);
int xhci_bulk_out(uint32_t hc_index, uint32_t port, uint8_t ep, const void *buf, uint32_t len);
int xhci_bulk_in(uint32_t hc_index, uint32_t port, uint8_t ep, void *buf, uint32_t len, uint32_t *actual_out);
int xhci_configure_interrupt_in_endpoint(
    uint32_t hc_index, uint32_t port, uint8_t intr_in_ep, uint16_t intr_mps, uint8_t interval
);
int xhci_interrupt_in(uint32_t hc_index, uint32_t port, uint8_t ep, void *buf, uint32_t len, uint32_t *actual_out);
int xhci_control_out0(
    uint32_t hc_index, uint32_t port, uint8_t req_type, uint8_t req, uint16_t value, uint16_t index
);
