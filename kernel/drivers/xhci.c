#include <drivers/xhci.h>
#include <drivers/pci.h>
#include <drivers/tty.h>
#include <devctl.h>
#include <asm/mm.h>
#include <string.h>

#define XHCI_MAX_CONTROLLERS 8
#define XHCI_MAX_PORTS 32
#define XHCI_CMD_TRBS 64
#define XHCI_EVT_TRBS 64

/* PCI config space */
#define PCI_CMD_OFFSET 0x04
#define PCI_CMD_IO     (1u << 0)
#define PCI_CMD_MEM    (1u << 1)
#define PCI_CMD_BME    (1u << 2)

/* xHCI capability registers */
#define XHCI_CAPLENGTH   0x00
#define XHCI_HCIVERSION  0x02
#define XHCI_HCSPARAMS1  0x04
#define XHCI_HCCPARAMS1  0x10
#define XHCI_DBOFF       0x14
#define XHCI_RTSOFF      0x18

/* xHCI operational registers */
#define XHCI_USBCMD      0x00
#define XHCI_USBSTS      0x04
#define XHCI_CRCR_LO     0x18
#define XHCI_CRCR_HI     0x1C
#define XHCI_DCBAAP_LO   0x30
#define XHCI_DCBAAP_HI   0x34
#define XHCI_CONFIG      0x38
#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORT_STRIDE 0x10

/* xHCI runtime/interrupter registers */
#define XHCI_IR0_BASE    0x20
#define XHCI_IR_IMAN     0x00
#define XHCI_IR_ERSTSZ   0x08
#define XHCI_IR_ERSTBA_LO 0x10
#define XHCI_IR_ERSTBA_HI 0x14
#define XHCI_IR_ERDP_LO   0x18
#define XHCI_IR_ERDP_HI   0x1C

/* USBCMD / USBSTS bits */
#define XHCI_USBCMD_RUNSTOP (1u << 0)
#define XHCI_USBCMD_HCRST   (1u << 1)
#define XHCI_USBSTS_HCH     (1u << 0)

/* PORTSC bits */
#define XHCI_PORTSC_CCS     (1u << 0)
#define XHCI_PORTSC_PED     (1u << 1)
#define XHCI_PORTSC_OCA     (1u << 3)
#define XHCI_PORTSC_PR      (1u << 4)
#define XHCI_PORTSC_PP      (1u << 9)

/* xHCI legacy support capability */
#define XHCI_EXT_CAP_ID_LEGACY 1u
#define XHCI_USBLEGSUP_BIOS_OWNED (1u << 16)
#define XHCI_USBLEGSUP_OS_OWNED   (1u << 24)

/* TRB types / completion */
#define XHCI_TRB_SETUP_STAGE   2u
#define XHCI_TRB_DATA_STAGE    3u
#define XHCI_TRB_STATUS_STAGE  4u
#define XHCI_TRB_NORMAL        1u
#define XHCI_TRB_ENABLE_SLOT   9u
#define XHCI_TRB_DISABLE_SLOT 10u
#define XHCI_TRB_ADDRESS_DEVICE 11u
#define XHCI_TRB_CONFIG_EP     12u
#define XHCI_TRB_TRANSFER_EVT 32u
#define XHCI_TRB_CMD_CMPL     33u
#define XHCI_CMPL_SUCCESS      1u
#define XHCI_CMPL_SHORT_PACKET 13u
#define XHCI_TRB_CTRL_IOC      (1u << 5)
#define XHCI_TRB_CTRL_IDT      (1u << 6)

#define USB_DESC_DEVICE        1u
#define USB_DESC_CONFIG        2u
#define USB_DESC_INTERFACE     4u

typedef struct {
    uint32_t d0;
    uint32_t d1;
    uint32_t d2;
    uint32_t d3;
} xhci_trb_t;

typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t size;
    uint32_t rsvd;
} xhci_erst_ent_t;

typedef struct {
    volatile uint8_t *base;
    volatile uint8_t *op;
    volatile uint8_t *rt;
    volatile uint32_t *db;
    xhci_trb_t *cmd_ring;
    xhci_trb_t *evt_ring;
    xhci_erst_ent_t *erst;
    uint64_t *dcbaa;
    uint32_t cmd_idx;
    uint32_t cmd_cycle;
    uint32_t evt_idx;
    uint32_t evt_cycle;
    uint8_t ready;
} xhci_runtime_t;

typedef struct {
    xhci_trb_t *ring;
    uint8_t ready;
    uint8_t ep;
    uint8_t dci;
    uint8_t trb_idx;
    uint8_t trb_cycle;
    uint16_t mps;
} xhci_bulk_ep_t;

static xhci_info_t g_xhci[XHCI_MAX_CONTROLLERS];
static uint32_t g_xhci_count = 0;

typedef struct {
    xhci_info_t *hc;
    uint32_t index;
} xhci_hc_ctx_t;

typedef struct {
    xhci_hc_ctx_t *hc_ctx;
    uint32_t port;
} xhci_port_ctx_t;

typedef struct {
    xhci_hc_ctx_t *hc_ctx;
    uint32_t port;
} xhci_dev_ctx_t;

static xhci_hc_ctx_t g_xhci_hc_ctx[XHCI_MAX_CONTROLLERS];
static xhci_port_ctx_t g_xhci_port_ctx[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_dev_ctx_t g_xhci_dev_ctx[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static dev_usb_dev_info_t g_xhci_dev_state[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_runtime_t g_xhci_rt[XHCI_MAX_CONTROLLERS];
static uint32_t g_xhci_slot_id[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static uint32_t *g_xhci_in_ctx[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static uint32_t *g_xhci_out_ctx[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_trb_t *g_xhci_ep0_ring[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static uint8_t *g_xhci_desc_buf[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_iface_info_t g_xhci_iface_info[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_bulk_ep_t g_xhci_bulk_in[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_bulk_ep_t g_xhci_bulk_out[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];
static xhci_bulk_ep_t g_xhci_intr_in[XHCI_MAX_CONTROLLERS][XHCI_MAX_PORTS];

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t bcd_usb;
    uint8_t dev_class;
    uint8_t dev_subclass;
    uint8_t dev_proto;
    uint8_t max_packet0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_dev;
    uint8_t i_manufacturer;
    uint8_t i_product;
    uint8_t i_serial;
    uint8_t num_configs;
} __attribute__((packed)) usb_device_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t total_length;
    uint8_t num_interfaces;
    uint8_t config_value;
    uint8_t i_config;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed)) usb_config_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t iface_num;
    uint8_t alt_setting;
    uint8_t num_endpoints;
    uint8_t iface_class;
    uint8_t iface_subclass;
    uint8_t iface_proto;
    uint8_t i_iface;
} __attribute__((packed)) usb_iface_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t addr;
    uint8_t attr;
    uint16_t max_packet;
    uint8_t interval;
} __attribute__((packed)) usb_ep_desc_t;

static void log_u32_hex(uint32_t v) {
    char tmp[16];
    utoa(v, tmp, 16);
    tty_klog(tmp);
}

static void log_u32_dec(uint32_t v) {
    char tmp[16];
    utoa(v, tmp, 10);
    tty_klog(tmp);
}

static uint32_t mmio_r32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t*)(base + off);
}

static void mmio_w32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(base + off) = v;
}

static void mmio_w64(volatile uint8_t *base, uint32_t off, uint64_t v) {
    mmio_w32(base, off, (uint32_t)(v & 0xFFFFFFFFu));
    mmio_w32(base, off + 4, (uint32_t)(v >> 32));
}

static int xhci_wait_bit(volatile uint32_t *reg, uint32_t mask, uint32_t expect_set, uint32_t loops) {
    for (uint32_t i = 0; i < loops; i++) {
        uint32_t v = *reg;
        if (expect_set) {
            if ((v & mask) != 0) return 0;
        } else {
            if ((v & mask) == 0) return 0;
        }
    }
    return -1;
}

static uint32_t pci_cfg_read32_local(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint16_t lo;
    uint16_t hi;
    lo = pci_cfg_read16(bus, slot, func, offset);
    hi = pci_cfg_read16(bus, slot, func, (uint8_t)(offset + 2u));
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

static void pci_cfg_write32_local(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    pci_cfg_write16(bus, slot, func, offset, (uint16_t)(value & 0xFFFFu));
    pci_cfg_write16(bus, slot, func, (uint8_t)(offset + 2u), (uint16_t)((value >> 16) & 0xFFFFu));
}

static void xhci_intel_route_ports(const pci_device_t *d) {
    /* Intel 8-series style USB2/USB3 routing registers. */
    const uint8_t XUSB2PR  = 0xD0;
    const uint8_t XUSB2PRM = 0xD4;
    const uint8_t USB3_PSSEN = 0xD8;
    const uint8_t USB3PRM  = 0xDC;
    uint32_t usb2m;
    uint32_t usb3m;
    if (!d) return;
    if (d->vendor_id != 0x8086u) return;
    usb2m = pci_cfg_read32_local(d->bus, d->slot, d->func, XUSB2PRM);
    usb3m = pci_cfg_read32_local(d->bus, d->slot, d->func, USB3PRM);
    tty_klog("xhci: route usb2m=");
    log_u32_hex(usb2m);
    tty_klog(" usb3m=");
    log_u32_hex(usb3m);
    tty_klog("\n");
    if (usb2m) pci_cfg_write32_local(d->bus, d->slot, d->func, XUSB2PR, usb2m);
    if (usb3m) pci_cfg_write32_local(d->bus, d->slot, d->func, USB3_PSSEN, usb3m);
}

static void xhci_legacy_handoff(volatile uint8_t *base) {
    uint32_t hcc;
    uint32_t xecp;
    if (!base) return;
    hcc = mmio_r32(base, XHCI_HCCPARAMS1);
    xecp = ((hcc >> 16) & 0xFFFFu) * 4u;
    while (xecp >= 0x40u && xecp < 0x1000u) {
        uint32_t cap = mmio_r32(base, xecp);
        uint32_t id = cap & 0xFFu;
        uint32_t next = ((cap >> 8) & 0xFFu) * 4u;
        if (id == XHCI_EXT_CAP_ID_LEGACY) {
            uint32_t v = mmio_r32(base, xecp);
            if (v & XHCI_USBLEGSUP_BIOS_OWNED) {
                mmio_w32(base, xecp, v | XHCI_USBLEGSUP_OS_OWNED);
                for (uint32_t i = 0; i < 2000000u; i++) {
                    v = mmio_r32(base, xecp);
                    if ((v & XHCI_USBLEGSUP_BIOS_OWNED) == 0) break;
                }
            }
            mmio_w32(base, xecp + 4u, 0u);
            break;
        }
        if (next == 0) break;
        xecp = next;
    }
}

static int xhci_skip_hard_reset_quirk(const pci_device_t *d) {
    if (!d) return 0;
    /* Intel 8-series xHCI (8086:8c31) can lose port state with our partial reset path. */
    if (d->vendor_id == 0x8086u && d->device_id == 0x8c31u) return 1;
    return 0;
}

static uint32_t xhci_portsc_read(const xhci_info_t *hc, uint32_t port) {
    volatile uint8_t *base;
    volatile uint32_t *portsc;
    if (!hc || port == 0 || port > hc->max_ports) return 0;
    base = (volatile uint8_t*)(uintptr_t)hc->mmio_base;
    portsc = (volatile uint32_t*)(base + hc->cap_length + XHCI_PORTSC_BASE);
    return portsc[(port - 1u) * (XHCI_PORT_STRIDE / 4u)];
}

static void xhci_portsc_write(const xhci_info_t *hc, uint32_t port, uint32_t value) {
    volatile uint8_t *base;
    volatile uint32_t *portsc;
    if (!hc || port == 0 || port > hc->max_ports) return;
    base = (volatile uint8_t*)(uintptr_t)hc->mmio_base;
    portsc = (volatile uint32_t*)(base + hc->cap_length + XHCI_PORTSC_BASE);
    portsc[(port - 1u) * (XHCI_PORT_STRIDE / 4u)] = value;
}

static uint32_t xhci_port_speed(uint32_t portsc) {
    return (portsc >> 10) & 0xFu;
}

static int xhci_speed_is_usb2(uint32_t s) {
    return (s == 1u || s == 2u || s == 3u) ? 1 : 0;
}

static int xhci_usbms_vidpid_fallback(uint16_t vid, uint16_t pid) {
    if (vid == 0x46f4u && pid == 0x0001u) return 1; /* qemu usb-storage */
    if (vid == 0x24a9u && pid == 0x205au) return 1; /* ASolid USB_0114 */
    return 0;
}

static uint8_t xhci_dci_from_ep(uint8_t ep, int in_dir) {
    if (ep == 0) return 0;
    return (uint8_t)(in_dir ? (ep * 2u + 1u) : (ep * 2u));
}

static uint32_t *xhci_ctx_ptr(uint32_t *base, uint8_t dci) {
    if (!base || dci == 0) return NULL;
    return &base[(uint32_t)(dci + 1u) * 8u];
}

static int xhci_addr_in_use(uint32_t hc_index, uint32_t except_port, uint32_t addr) {
    if (hc_index >= XHCI_MAX_CONTROLLERS || addr == 0) return 0;
    for (uint32_t p = 1; p <= XHCI_MAX_PORTS; p++) {
        if (p == except_port) continue;
        if (g_xhci_dev_state[hc_index][p - 1].address == addr) return 1;
    }
    return 0;
}

static void xhci_refresh_connected_ports(xhci_info_t *hc) {
    uint8_t connected = 0;
    if (!hc) return;
    for (uint32_t p = 1; p <= hc->max_ports; p++) {
        uint32_t ps = xhci_portsc_read(hc, p);
        if (ps & XHCI_PORTSC_CCS) connected++;
    }
    hc->connected_ports = connected;
}

static int xhci_setup_runtime(uint32_t hc_index, xhci_info_t *hc) {
    xhci_runtime_t *rt;
    uint32_t dboff;
    uint32_t rtsoff;
    uint32_t max_slots;
    uint16_t cmd;

    if (!hc || hc_index >= XHCI_MAX_CONTROLLERS) return -1;
    rt = &g_xhci_rt[hc_index];
    memset(rt, 0, sizeof(*rt));

    rt->base = (volatile uint8_t*)(uintptr_t)hc->mmio_base;
    dboff = mmio_r32(rt->base, XHCI_DBOFF) & ~0x3u;
    rtsoff = mmio_r32(rt->base, XHCI_RTSOFF) & ~0x1Fu;
    rt->op = rt->base + hc->cap_length;
    rt->rt = rt->base + rtsoff;
    rt->db = (volatile uint32_t*)(rt->base + dboff);

    rt->cmd_ring = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * XHCI_CMD_TRBS, 64);
    rt->evt_ring = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * XHCI_EVT_TRBS, 64);
    rt->erst = (xhci_erst_ent_t*)valloc_aligned(sizeof(xhci_erst_ent_t), 64);
    rt->dcbaa = (uint64_t*)valloc_aligned(2048, 64);
    if (!rt->cmd_ring || !rt->evt_ring || !rt->erst || !rt->dcbaa) return -1;

    memset(rt->cmd_ring, 0, sizeof(xhci_trb_t) * XHCI_CMD_TRBS);
    memset(rt->evt_ring, 0, sizeof(xhci_trb_t) * XHCI_EVT_TRBS);
    memset(rt->erst, 0, sizeof(xhci_erst_ent_t));
    memset(rt->dcbaa, 0, 2048);

    rt->cmd_idx = 0;
    rt->cmd_cycle = 1;
    rt->evt_idx = 0;
    rt->evt_cycle = 1;

    rt->erst[0].addr_lo = (uint32_t)(uintptr_t)rt->evt_ring;
    rt->erst[0].addr_hi = 0;
    rt->erst[0].size = XHCI_EVT_TRBS;

    mmio_w64(rt->op, XHCI_DCBAAP_LO, (uint64_t)(uintptr_t)rt->dcbaa);

    mmio_w32(rt->rt + XHCI_IR0_BASE, XHCI_IR_IMAN, 0);
    mmio_w32(rt->rt + XHCI_IR0_BASE, XHCI_IR_ERSTSZ, 1);
    mmio_w64(rt->rt + XHCI_IR0_BASE, XHCI_IR_ERSTBA_LO, (uint64_t)(uintptr_t)rt->erst);
    mmio_w64(rt->rt + XHCI_IR0_BASE, XHCI_IR_ERDP_LO, (uint64_t)(uintptr_t)rt->evt_ring);

    mmio_w64(rt->op, XHCI_CRCR_LO, ((uint64_t)(uintptr_t)rt->cmd_ring) | 1u);

    max_slots = (mmio_r32(rt->base, XHCI_HCSPARAMS1) & 0xFFu);
    if (max_slots == 0) max_slots = 8;
    mmio_w32(rt->op, XHCI_CONFIG, max_slots);

    cmd = pci_cfg_read16(hc->bus, hc->slot, hc->func, PCI_CMD_OFFSET);
    cmd |= (uint16_t)(PCI_CMD_MEM | PCI_CMD_BME);
    cmd &= (uint16_t)~PCI_CMD_IO;
    pci_cfg_write16(hc->bus, hc->slot, hc->func, PCI_CMD_OFFSET, cmd);

    mmio_w32(rt->op, XHCI_USBCMD, mmio_r32(rt->op, XHCI_USBCMD) | XHCI_USBCMD_RUNSTOP);
    (void)xhci_wait_bit((volatile uint32_t*)(rt->op + XHCI_USBSTS), XHCI_USBSTS_HCH, 0, 1000000);

    rt->ready = 1;
    return 0;
}

static int xhci_event_pop(
    xhci_runtime_t *rt, uint32_t *type_out, uint32_t *cc_out, uint32_t *slot_out,
    uint64_t *ptr_out, uint32_t *status_out, uint32_t loops
) {
    for (uint32_t i = 0; i < loops; i++) {
        xhci_trb_t *ev = &rt->evt_ring[rt->evt_idx];
        uint32_t cycle = ev->d3 & 1u;
        if (cycle != rt->evt_cycle) continue;

        if (type_out) *type_out = (ev->d3 >> 10) & 0x3Fu;
        if (cc_out) *cc_out = (ev->d2 >> 24) & 0xFFu;
        if (slot_out) *slot_out = (ev->d3 >> 24) & 0xFFu;
        if (ptr_out) *ptr_out = ((uint64_t)ev->d1 << 32) | (uint64_t)ev->d0;
        if (status_out) *status_out = ev->d2 & 0x00FFFFFFu;

        rt->evt_idx++;
        if (rt->evt_idx >= XHCI_EVT_TRBS) {
            rt->evt_idx = 0;
            rt->evt_cycle ^= 1u;
        }
        mmio_w64(
            rt->rt + XHCI_IR0_BASE, XHCI_IR_ERDP_LO,
            ((uint64_t)(uintptr_t)&rt->evt_ring[rt->evt_idx]) | (1u << 3)
        );
        return 0;
    }
    return -1;
}

static int xhci_cmd_poll_completion(xhci_runtime_t *rt, uint32_t *slot_id_out, uint32_t loops) {
    for (uint32_t i = 0; i < loops; i++) {
        uint32_t type = 0, cc = 0, slot = 0;
        if (xhci_event_pop(rt, &type, &cc, &slot, NULL, NULL, 1) != 0) continue;
        if (type != XHCI_TRB_CMD_CMPL) continue;
        if (cc != XHCI_CMPL_SUCCESS) return -1;
        if (slot_id_out) *slot_id_out = slot;
        return 0;
    }
    return -1;
}

static int xhci_wait_transfer_event(
    xhci_runtime_t *rt, uint32_t slot_id, uint32_t loops, int allow_short, uint32_t *residual_out
) {
    for (uint32_t i = 0; i < loops; i++) {
        uint32_t type = 0, cc = 0, slot = 0, status = 0;
        if (xhci_event_pop(rt, &type, &cc, &slot, NULL, &status, 1) != 0) continue;
        if (type != XHCI_TRB_TRANSFER_EVT) continue;
        if (slot != slot_id) continue;
        if (cc != XHCI_CMPL_SUCCESS) {
            if (!(allow_short && cc == XHCI_CMPL_SHORT_PACKET)) return -1;
        }
        if (residual_out) *residual_out = status;
        return 0;
    }
    return -1;
}

static int xhci_cmd_submit_ex(
    uint32_t hc_index, uint64_t param, uint32_t trb_type, uint32_t slot_id, uint32_t *out_slot
) {
    xhci_runtime_t *rt;
    xhci_trb_t *trb;
    uint32_t ctrl;

    if (hc_index >= XHCI_MAX_CONTROLLERS) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    /* Keep the last TRB free and wrap producer manually. */
    if (rt->cmd_idx >= XHCI_CMD_TRBS - 1) {
        rt->cmd_idx = 0;
        rt->cmd_cycle ^= 1u;
    }

    trb = &rt->cmd_ring[rt->cmd_idx];
    trb->d0 = (uint32_t)(param & 0xFFFFFFFFu);
    trb->d1 = (uint32_t)(param >> 32);
    trb->d2 = 0;
    ctrl = (trb_type << 10) | (rt->cmd_cycle & 1u);
    if (slot_id) ctrl |= (slot_id << 24);
    trb->d3 = ctrl;

    rt->cmd_idx++;
    if (rt->cmd_idx >= XHCI_CMD_TRBS - 1) {
        rt->cmd_idx = 0;
        rt->cmd_cycle ^= 1u;
    }
    rt->db[0] = 0;

    return xhci_cmd_poll_completion(rt, out_slot, 2000000);
}

static int xhci_cmd_submit(uint32_t hc_index, uint32_t trb_type, uint32_t slot_id, uint32_t *out_slot) {
    return xhci_cmd_submit_ex(hc_index, 0, trb_type, slot_id, out_slot);
}

static int xhci_enable_slot(uint32_t hc_index, uint32_t *slot_out) {
    return xhci_cmd_submit(hc_index, XHCI_TRB_ENABLE_SLOT, 0, slot_out);
}

static int xhci_disable_slot(uint32_t hc_index, uint32_t slot_id) {
    uint32_t tmp = 0;
    return xhci_cmd_submit(hc_index, XHCI_TRB_DISABLE_SLOT, slot_id & 0xFFu, &tmp);
}

static uint32_t xhci_ep0_mps(uint32_t speed) {
    if (speed >= 4u) return 512u;
    if (speed == 3u) return 64u;
    return 8u;
}

static int xhci_address_device(
    uint32_t hc_index, uint32_t port, uint32_t speed, uint32_t slot_id, uint32_t *usb_addr_out
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t *in_ctx;
    uint32_t *out_ctx;
    xhci_trb_t *ep0_ring;
    uint32_t *slot_ctx;
    uint32_t *ep0_ctx;
    uint32_t evt_slot = 0;
    uint32_t usb_addr;

    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS || slot_id == 0) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;

    if (!g_xhci_in_ctx[hc_index][pidx]) {
        g_xhci_in_ctx[hc_index][pidx] = (uint32_t*)valloc_aligned(4096, 64);
        g_xhci_out_ctx[hc_index][pidx] = (uint32_t*)valloc_aligned(4096, 64);
        g_xhci_ep0_ring[hc_index][pidx] = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * 16u, 64);
    }
    in_ctx = g_xhci_in_ctx[hc_index][pidx];
    out_ctx = g_xhci_out_ctx[hc_index][pidx];
    ep0_ring = g_xhci_ep0_ring[hc_index][pidx];
    if (!in_ctx || !out_ctx || !ep0_ring) return -1;

    memset(in_ctx, 0, 4096);
    memset(out_ctx, 0, 4096);
    memset(ep0_ring, 0, sizeof(xhci_trb_t) * 16u);
    ep0_ring[15].d0 = (uint32_t)(uintptr_t)ep0_ring;
    ep0_ring[15].d1 = 0;
    ep0_ring[15].d2 = 0;
    ep0_ring[15].d3 = (6u << 10) | 1u;

    rt->dcbaa[slot_id] = (uint64_t)(uintptr_t)out_ctx;

    in_ctx[1] = 0x3u;
    slot_ctx = &in_ctx[8];
    ep0_ctx = &in_ctx[16];
    slot_ctx[0] = (1u << 27) | ((speed & 0xFu) << 20);
    slot_ctx[1] = (port & 0xFFu) << 16;
    ep0_ctx[1] = xhci_ep0_mps(speed);
    ep0_ctx[2] = ((uint32_t)(uintptr_t)ep0_ring & ~0xFu) | 1u;
    ep0_ctx[4] = 8u;

    if (xhci_cmd_submit_ex(
            hc_index, (uint64_t)(uintptr_t)in_ctx, XHCI_TRB_ADDRESS_DEVICE, slot_id, &evt_slot
        ) != 0) return -1;

    usb_addr = out_ctx[3] & 0xFFu;
    if (usb_addr_out) *usb_addr_out = usb_addr;
    return 0;
}

static int xhci_control_in(
    uint32_t hc_index, uint32_t port, uint32_t slot_id,
    uint8_t req_type, uint8_t req, uint16_t value, uint16_t index,
    uint16_t len, void *out_buf, uint16_t out_buf_size, uint16_t *actual_len_out
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    xhci_trb_t *ep0_ring;
    uint8_t *buf;
    uint64_t setup_pkt;

    if (!out_buf || len == 0 || out_buf_size < len) return -1;
    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS || slot_id == 0) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    ep0_ring = g_xhci_ep0_ring[hc_index][pidx];
    if (!ep0_ring) return -1;

    if (!g_xhci_desc_buf[hc_index][pidx]) {
        g_xhci_desc_buf[hc_index][pidx] = (uint8_t*)valloc_aligned(512, 64);
    }
    buf = g_xhci_desc_buf[hc_index][pidx];
    if (!buf) return -1;
    memset(buf, 0, 512);
    memset(ep0_ring, 0, sizeof(xhci_trb_t) * 16u);
    ep0_ring[15].d0 = (uint32_t)(uintptr_t)ep0_ring;
    ep0_ring[15].d1 = 0;
    ep0_ring[15].d2 = 0;
    ep0_ring[15].d3 = (6u << 10) | 1u;

    setup_pkt = 0;
    setup_pkt |= (uint64_t)req_type;
    setup_pkt |= ((uint64_t)req << 8);
    setup_pkt |= ((uint64_t)value << 16);
    setup_pkt |= ((uint64_t)index << 32);
    setup_pkt |= ((uint64_t)len << 48);

    ep0_ring[0].d0 = (uint32_t)(setup_pkt & 0xFFFFFFFFu);
    ep0_ring[0].d1 = (uint32_t)(setup_pkt >> 32);
    ep0_ring[0].d2 = 8u;
    ep0_ring[0].d3 = (XHCI_TRB_SETUP_STAGE << 10) | XHCI_TRB_CTRL_IDT | (3u << 16) | 1u;

    ep0_ring[1].d0 = (uint32_t)(uintptr_t)buf;
    ep0_ring[1].d1 = 0;
    ep0_ring[1].d2 = (uint32_t)len;
    ep0_ring[1].d3 = (XHCI_TRB_DATA_STAGE << 10) | (1u << 16) | 1u;

    ep0_ring[2].d0 = 0;
    ep0_ring[2].d1 = 0;
    ep0_ring[2].d2 = 0;
    ep0_ring[2].d3 = (XHCI_TRB_STATUS_STAGE << 10) | XHCI_TRB_CTRL_IOC | 1u;

    rt->db[slot_id] = 1u;
    {
        uint32_t residual = 0;
        uint16_t actual;
        if (xhci_wait_transfer_event(rt, slot_id, 2000000, 1, &residual) != 0) return -1;
        if (residual > len) residual = len;
        actual = (uint16_t)(len - residual);
        if (actual == 0) return -1;
        memcpy(out_buf, buf, actual);
        if (actual_len_out) *actual_len_out = actual;
    }
    return 0;
}

int xhci_control_out0(
    uint32_t hc_index, uint32_t port, uint8_t req_type, uint8_t req, uint16_t value, uint16_t index
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t slot_id;
    xhci_trb_t *ep0_ring;
    uint64_t setup_pkt;
    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    slot_id = g_xhci_slot_id[hc_index][pidx];
    if (slot_id == 0) return -1;
    ep0_ring = g_xhci_ep0_ring[hc_index][pidx];
    if (!ep0_ring) return -1;

    memset(ep0_ring, 0, sizeof(xhci_trb_t) * 16u);
    ep0_ring[15].d0 = (uint32_t)(uintptr_t)ep0_ring;
    ep0_ring[15].d1 = 0;
    ep0_ring[15].d2 = 0;
    ep0_ring[15].d3 = (6u << 10) | 1u;

    setup_pkt = 0;
    setup_pkt |= (uint64_t)req_type;
    setup_pkt |= ((uint64_t)req << 8);
    setup_pkt |= ((uint64_t)value << 16);
    setup_pkt |= ((uint64_t)index << 32);

    ep0_ring[0].d0 = (uint32_t)(setup_pkt & 0xFFFFFFFFu);
    ep0_ring[0].d1 = (uint32_t)(setup_pkt >> 32);
    ep0_ring[0].d2 = 8u;
    ep0_ring[0].d3 = (XHCI_TRB_SETUP_STAGE << 10) | XHCI_TRB_CTRL_IDT | (0u << 16) | 1u;

    ep0_ring[1].d0 = 0;
    ep0_ring[1].d1 = 0;
    ep0_ring[1].d2 = 0;
    ep0_ring[1].d3 = (XHCI_TRB_STATUS_STAGE << 10) | XHCI_TRB_CTRL_IOC | (1u << 16) | 1u;

    rt->db[slot_id] = 1u;
    return xhci_wait_transfer_event(rt, slot_id, 2000000, 1, NULL);
}

static int xhci_get_device_desc(uint32_t hc_index, uint32_t port, uint32_t slot_id, usb_device_desc_t *out_desc) {
    uint16_t actual = 0;
    if (!out_desc) return -1;
    if (xhci_control_in(
            hc_index, port, slot_id, 0x80u, 6u, (uint16_t)(USB_DESC_DEVICE << 8), 0u,
            (uint16_t)sizeof(*out_desc), out_desc, (uint16_t)sizeof(*out_desc), &actual
        ) != 0) return -1;
    if (actual < 8) return -1;
    if (out_desc->len < 18 || out_desc->type != USB_DESC_DEVICE) return -1;
    return 0;
}

static int xhci_get_interface_info(
    uint32_t hc_index, uint32_t port, uint32_t slot_id, xhci_iface_info_t *out
) {
    uint8_t hdr[9];
    uint8_t buf[512];
    uint16_t total = 0;
    uint32_t off = 0;
    int have_iface = 0;
    uint8_t cur_iface = 0xFF;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    memset(hdr, 0, sizeof(hdr));
    if (xhci_control_in(
            hc_index, port, slot_id, 0x80u, 6u, (uint16_t)(USB_DESC_CONFIG << 8), 0u,
            (uint16_t)sizeof(hdr), hdr, (uint16_t)sizeof(hdr), NULL
        ) != 0) return -1;
    if (hdr[0] < 9 || hdr[1] != USB_DESC_CONFIG) return -1;
    total = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    if (total < 9) return -1;
    if (total > sizeof(buf)) total = sizeof(buf);

    memset(buf, 0, sizeof(buf));
    if (xhci_control_in(
            hc_index, port, slot_id, 0x80u, 6u, (uint16_t)(USB_DESC_CONFIG << 8), 0u,
            total, buf, (uint16_t)sizeof(buf), &total
        ) != 0) return -1;
    if (buf[0] < 9 || buf[1] != USB_DESC_CONFIG) return -1;

    while (off + 2 <= total) {
        uint8_t dlen = buf[off];
        uint8_t dtype = buf[off + 1];
        if (dlen < 2 || off + dlen > total) break;
        if (dtype == USB_DESC_INTERFACE && dlen >= 9) {
            usb_iface_desc_t *ifd = (usb_iface_desc_t*)&buf[off];
            if (!have_iface) {
                have_iface = 1;
                cur_iface = ifd->iface_num;
                out->iface_num = ifd->iface_num;
                out->iface_class = ifd->iface_class;
                out->iface_subclass = ifd->iface_subclass;
                out->iface_proto = ifd->iface_proto;
            } else if (ifd->iface_num != cur_iface) {
                break;
            }
        } else if (dtype == 5u && dlen >= 7 && have_iface) {
            usb_ep_desc_t *ep = (usb_ep_desc_t*)&buf[off];
            if ((ep->attr & 0x3u) == 2u) {
                if (ep->addr & 0x80u) {
                    if (out->bulk_in_ep == 0) out->bulk_in_ep = ep->addr & 0x0Fu;
                } else {
                    if (out->bulk_out_ep == 0) out->bulk_out_ep = ep->addr & 0x0Fu;
                }
                if (out->bulk_mps == 0) out->bulk_mps = ep->max_packet;
            } else if ((ep->attr & 0x3u) == 3u) {
                if ((ep->addr & 0x80u) && out->intr_in_ep == 0) {
                    out->intr_in_ep = ep->addr & 0x0Fu;
                    out->intr_mps = ep->max_packet;
                    out->intr_interval = ep->interval;
                }
            }
        }
        off += dlen;
    }
    if (!have_iface) return -1;
    out->valid = 1;
    return 0;
}

int xhci_configure_bulk_endpoints(
    uint32_t hc_index, uint32_t port, uint8_t bulk_in_ep, uint8_t bulk_out_ep, uint16_t bulk_mps
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t slot_id;
    uint8_t dci_in;
    uint8_t dci_out;
    uint32_t *in_ctx;
    uint32_t *out_ctx;
    uint32_t *slot_ctx;
    uint32_t *ep_in_ctx;
    uint32_t *ep_out_ctx;
    uint32_t evt_slot = 0;
    xhci_bulk_ep_t *in_ep;
    xhci_bulk_ep_t *out_ep;

    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS) return -1;
    if (bulk_in_ep == 0 || bulk_out_ep == 0) return -1;
    if (bulk_mps == 0) bulk_mps = 512u;

    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    slot_id = g_xhci_slot_id[hc_index][pidx];
    in_ctx = g_xhci_in_ctx[hc_index][pidx];
    out_ctx = g_xhci_out_ctx[hc_index][pidx];
    if (slot_id == 0 || !in_ctx || !out_ctx) return -1;

    dci_in = xhci_dci_from_ep(bulk_in_ep, 1);
    dci_out = xhci_dci_from_ep(bulk_out_ep, 0);
    if (dci_in == 0 || dci_out == 0) return -1;

    in_ep = &g_xhci_bulk_in[hc_index][pidx];
    out_ep = &g_xhci_bulk_out[hc_index][pidx];
    if (!in_ep->ring) in_ep->ring = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * 16u, 64);
    if (!out_ep->ring) out_ep->ring = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * 16u, 64);
    if (!in_ep->ring || !out_ep->ring) return -1;

    memset(in_ep->ring, 0, sizeof(xhci_trb_t) * 16u);
    memset(out_ep->ring, 0, sizeof(xhci_trb_t) * 16u);
    in_ep->ring[15].d0 = (uint32_t)(uintptr_t)in_ep->ring;
    in_ep->ring[15].d3 = (6u << 10) | (1u << 1) | 1u;
    out_ep->ring[15].d0 = (uint32_t)(uintptr_t)out_ep->ring;
    out_ep->ring[15].d3 = (6u << 10) | (1u << 1) | 1u;

    memset(in_ctx, 0, 4096);
    in_ctx[1] = (1u << 0) | (1u << dci_in) | (1u << dci_out);
    slot_ctx = &in_ctx[8];
    memcpy(slot_ctx, &out_ctx[8], 8u * sizeof(uint32_t));

    ep_out_ctx = xhci_ctx_ptr(in_ctx, dci_out);
    ep_in_ctx = xhci_ctx_ptr(in_ctx, dci_in);
    if (!ep_out_ctx || !ep_in_ctx) return -1;

    /* Bulk OUT endpoint context. */
    ep_out_ctx[0] = (3u << 1); /* CErr */
    ep_out_ctx[1] = ((uint32_t)bulk_mps << 16) | (2u << 3);
    ep_out_ctx[2] = ((uint32_t)(uintptr_t)out_ep->ring & ~0xFu) | 1u;
    ep_out_ctx[4] = 512u;

    /* Bulk IN endpoint context. */
    ep_in_ctx[0] = (3u << 1); /* CErr */
    ep_in_ctx[1] = ((uint32_t)bulk_mps << 16) | (6u << 3);
    ep_in_ctx[2] = ((uint32_t)(uintptr_t)in_ep->ring & ~0xFu) | 1u;
    ep_in_ctx[4] = 512u;

    if (xhci_cmd_submit_ex(
            hc_index, (uint64_t)(uintptr_t)in_ctx, XHCI_TRB_CONFIG_EP, slot_id, &evt_slot
        ) != 0) return -1;

    in_ep->ready = 1;
    in_ep->ep = bulk_in_ep;
    in_ep->dci = dci_in;
    in_ep->trb_idx = 0;
    in_ep->trb_cycle = 1;
    in_ep->mps = bulk_mps;
    out_ep->ready = 1;
    out_ep->ep = bulk_out_ep;
    out_ep->dci = dci_out;
    out_ep->trb_idx = 0;
    out_ep->trb_cycle = 1;
    out_ep->mps = bulk_mps;
    return 0;
}

int xhci_configure_interrupt_in_endpoint(
    uint32_t hc_index, uint32_t port, uint8_t intr_in_ep, uint16_t intr_mps, uint8_t interval
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t slot_id;
    uint8_t dci_in;
    uint32_t *in_ctx;
    uint32_t *out_ctx;
    uint32_t *slot_ctx;
    uint32_t *ep_in_ctx;
    uint32_t evt_slot = 0;
    xhci_bulk_ep_t *in_ep;

    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS) return -1;
    if (intr_in_ep == 0) return -1;
    if (intr_mps == 0) intr_mps = 8u;
    if (interval == 0) interval = 10u;

    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    slot_id = g_xhci_slot_id[hc_index][pidx];
    in_ctx = g_xhci_in_ctx[hc_index][pidx];
    out_ctx = g_xhci_out_ctx[hc_index][pidx];
    if (slot_id == 0 || !in_ctx || !out_ctx) return -1;

    dci_in = xhci_dci_from_ep(intr_in_ep, 1);
    if (dci_in == 0) return -1;

    in_ep = &g_xhci_intr_in[hc_index][pidx];
    if (!in_ep->ring) in_ep->ring = (xhci_trb_t*)valloc_aligned(sizeof(xhci_trb_t) * 16u, 64);
    if (!in_ep->ring) return -1;

    memset(in_ep->ring, 0, sizeof(xhci_trb_t) * 16u);
    in_ep->ring[15].d0 = (uint32_t)(uintptr_t)in_ep->ring;
    in_ep->ring[15].d3 = (6u << 10) | (1u << 1) | 1u;

    memset(in_ctx, 0, 4096);
    in_ctx[1] = (1u << 0) | (1u << dci_in);
    slot_ctx = &in_ctx[8];
    memcpy(slot_ctx, &out_ctx[8], 8u * sizeof(uint32_t));

    ep_in_ctx = xhci_ctx_ptr(in_ctx, dci_in);
    if (!ep_in_ctx) return -1;
    ep_in_ctx[0] = (3u << 1) | ((uint32_t)interval << 16);
    ep_in_ctx[1] = ((uint32_t)intr_mps << 16) | (7u << 3);
    ep_in_ctx[2] = ((uint32_t)(uintptr_t)in_ep->ring & ~0xFu) | 1u;
    ep_in_ctx[4] = intr_mps;

    if (xhci_cmd_submit_ex(
            hc_index, (uint64_t)(uintptr_t)in_ctx, XHCI_TRB_CONFIG_EP, slot_id, &evt_slot
        ) != 0) return -1;

    in_ep->ready = 1;
    in_ep->ep = intr_in_ep;
    in_ep->dci = dci_in;
    in_ep->trb_idx = 0;
    in_ep->trb_cycle = 1;
    in_ep->mps = intr_mps;
    return 0;
}

int xhci_interrupt_in(uint32_t hc_index, uint32_t port, uint8_t ep, void *buf, uint32_t len, uint32_t *actual_out) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t slot_id;
    xhci_bulk_ep_t *st;
    uint32_t residual = 0;
    uint32_t idx;
    if (!buf || len == 0) return -1;
    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    slot_id = g_xhci_slot_id[hc_index][pidx];
    if (slot_id == 0) return -1;

    st = &g_xhci_intr_in[hc_index][pidx];
    if (!st->ready || !st->ring || st->ep != ep) return -1;
    idx = st->trb_idx;
    if (idx >= 15u) {
        idx = 0;
        st->trb_cycle ^= 1u;
    }

    memset(&st->ring[idx], 0, sizeof(xhci_trb_t));
    st->ring[idx].d0 = (uint32_t)(uintptr_t)buf;
    st->ring[idx].d1 = 0;
    st->ring[idx].d2 = len;
    st->ring[idx].d3 = (XHCI_TRB_NORMAL << 10) | XHCI_TRB_CTRL_IOC | (st->trb_cycle & 1u);
    rt->db[slot_id] = st->dci;
    if (xhci_wait_transfer_event(rt, slot_id, 200000, 1, &residual) != 0) return -1;
    if (residual > len) residual = len;
    if (actual_out) *actual_out = len - residual;
    idx++;
    if (idx >= 15u) {
        idx = 0;
        st->trb_cycle ^= 1u;
    }
    st->trb_idx = (uint8_t)idx;
    return 0;
}

static int xhci_bulk_xfer(
    uint32_t hc_index, uint32_t port, uint8_t ep, int in_dir, void *buf, uint32_t len, uint32_t *actual_out
) {
    xhci_runtime_t *rt;
    uint32_t pidx;
    uint32_t slot_id;
    xhci_bulk_ep_t *st;
    uint32_t residual = 0;
    uint32_t idx;

    if (!buf || len == 0) return -1;
    if (hc_index >= XHCI_MAX_CONTROLLERS || port == 0 || port > XHCI_MAX_PORTS) return -1;
    rt = &g_xhci_rt[hc_index];
    if (!rt->ready) return -1;
    pidx = port - 1;
    slot_id = g_xhci_slot_id[hc_index][pidx];
    if (slot_id == 0) return -1;

    st = in_dir ? &g_xhci_bulk_in[hc_index][pidx] : &g_xhci_bulk_out[hc_index][pidx];
    if (!st->ready || !st->ring || st->ep != ep) return -1;
    if (!g_xhci_bulk_in[hc_index][pidx].ready || !g_xhci_bulk_out[hc_index][pidx].ready) return -1;
    idx = st->trb_idx;
    if (idx >= 15u) {
        idx = 0;
        st->trb_cycle ^= 1u;
    }

    memset(&st->ring[idx], 0, sizeof(xhci_trb_t));
    st->ring[idx].d0 = (uint32_t)(uintptr_t)buf;
    st->ring[idx].d1 = 0;
    st->ring[idx].d2 = len;
    st->ring[idx].d3 = (XHCI_TRB_NORMAL << 10) | XHCI_TRB_CTRL_IOC | (st->trb_cycle & 1u);

    rt->db[slot_id] = st->dci;
    if (xhci_wait_transfer_event(rt, slot_id, 2000000, in_dir ? 1 : 0, &residual) != 0) return -1;
    if (residual > len) residual = len;
    if (actual_out) *actual_out = len - residual;
    idx++;
    if (idx >= 15u) {
        idx = 0;
        st->trb_cycle ^= 1u;
    }
    st->trb_idx = (uint8_t)idx;
    return 0;
}

int xhci_bulk_out(uint32_t hc_index, uint32_t port, uint8_t ep, const void *buf, uint32_t len) {
    return xhci_bulk_xfer(hc_index, port, ep, 0, (void*)buf, len, NULL);
}

int xhci_bulk_in(uint32_t hc_index, uint32_t port, uint8_t ep, void *buf, uint32_t len, uint32_t *actual_out) {
    return xhci_bulk_xfer(hc_index, port, ep, 1, buf, len, actual_out);
}

static void xhci_rescan_hc(xhci_hc_ctx_t *hc_ctx, int reset_ports) {
    xhci_info_t *hc = hc_ctx ? hc_ctx->hc : NULL;
    if (!hc) return;

    xhci_refresh_connected_ports(hc);
    for (uint32_t p = 1; p <= hc->max_ports && p <= XHCI_MAX_PORTS; p++) {
        uint32_t ps = xhci_portsc_read(hc, p);
        dev_usb_dev_info_t *st = &g_xhci_dev_state[hc_ctx->index][p - 1];

        if ((ps & XHCI_PORTSC_CCS) && reset_ports) {
            uint32_t sp = xhci_port_speed(ps);
            if ((ps & XHCI_PORTSC_PED) == 0 && xhci_speed_is_usb2(sp)) {
                xhci_portsc_write(hc, p, ps | XHCI_PORTSC_PR);
                for (uint32_t i = 0; i < 100000; i++) { (void)i; }
                ps = xhci_portsc_read(hc, p);
            }
        }

        st->hc_type = 1;
        st->hc_index = hc_ctx->index;
        st->port = p;
        st->present = (ps & XHCI_PORTSC_CCS) ? 1u : 0u;
        st->speed = xhci_port_speed(ps);
        st->slot_id = g_xhci_slot_id[hc_ctx->index][p - 1];

        if (st->present && st->address == 0 && g_xhci_rt[hc_ctx->index].ready) {
            uint32_t slot = g_xhci_slot_id[hc_ctx->index][p - 1];
            uint32_t addr = 0;
            st->vendor_id = 0;
            st->product_id = 0;
            st->dev_class = 0;
            st->dev_subclass = 0;
            st->dev_proto = 0;
            st->bulk_in_ep = 0;
            st->bulk_out_ep = 0;
            st->bulk_mps = 0;
            if (slot == 0 && xhci_enable_slot(hc_ctx->index, &slot) == 0) {
                g_xhci_slot_id[hc_ctx->index][p - 1] = slot;
                st->slot_id = slot;
            }
            if (slot != 0 &&
                xhci_address_device(hc_ctx->index, p, st->speed, slot, &addr) == 0 &&
                addr != 0 &&
                !xhci_addr_in_use(hc_ctx->index, p, addr)) {
                usb_device_desc_t dd;
                st->address = addr;
                if (xhci_get_device_desc(hc_ctx->index, p, slot, &dd) == 0) {
                    uint32_t addr_sync = 0;
                    st->vendor_id = dd.vendor_id;
                    st->product_id = dd.product_id;
                    st->dev_class = dd.dev_class;
                    st->dev_subclass = dd.dev_subclass;
                    st->dev_proto = dd.dev_proto;
                    /* Re-arm EP0 dequeue before next control transfer on the same slot. */
                    (void)xhci_address_device(hc_ctx->index, p, st->speed, slot, &addr_sync);
                    memset(&g_xhci_iface_info[hc_ctx->index][p - 1], 0, sizeof(xhci_iface_info_t));
                    if (xhci_get_interface_info(hc_ctx->index, p, slot, &g_xhci_iface_info[hc_ctx->index][p - 1]) == 0) {
                        st->dev_class = g_xhci_iface_info[hc_ctx->index][p - 1].iface_class;
                        st->dev_subclass = g_xhci_iface_info[hc_ctx->index][p - 1].iface_subclass;
                        st->dev_proto = g_xhci_iface_info[hc_ctx->index][p - 1].iface_proto;
                        st->bulk_in_ep = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_in_ep;
                        st->bulk_out_ep = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_out_ep;
                        st->bulk_mps = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_mps;
                    } else if (xhci_usbms_vidpid_fallback(st->vendor_id, st->product_id)) {
                        /* Known USBMS fallback while generic interface parsing is incomplete. */
                        st->dev_class = 0x08u;
                        st->dev_subclass = 0x06u;
                        st->dev_proto = 0x50u;
                        st->bulk_in_ep = 1u;
                        st->bulk_out_ep = 2u;
                        st->bulk_mps = (st->speed >= 4u) ? 1024u : 512u;
                    }
                }
            }
        }
        if (st->present && st->address && st->vendor_id == 0 && st->product_id == 0 && st->slot_id != 0) {
            usb_device_desc_t dd;
            if (xhci_get_device_desc(hc_ctx->index, p, st->slot_id, &dd) == 0) {
                uint32_t addr_sync = 0;
                st->vendor_id = dd.vendor_id;
                st->product_id = dd.product_id;
                st->dev_class = dd.dev_class;
                st->dev_subclass = dd.dev_subclass;
                st->dev_proto = dd.dev_proto;
                (void)xhci_address_device(hc_ctx->index, p, st->speed, st->slot_id, &addr_sync);
                memset(&g_xhci_iface_info[hc_ctx->index][p - 1], 0, sizeof(xhci_iface_info_t));
                if (xhci_get_interface_info(hc_ctx->index, p, st->slot_id, &g_xhci_iface_info[hc_ctx->index][p - 1]) == 0) {
                    st->dev_class = g_xhci_iface_info[hc_ctx->index][p - 1].iface_class;
                    st->dev_subclass = g_xhci_iface_info[hc_ctx->index][p - 1].iface_subclass;
                    st->dev_proto = g_xhci_iface_info[hc_ctx->index][p - 1].iface_proto;
                    st->bulk_in_ep = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_in_ep;
                    st->bulk_out_ep = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_out_ep;
                    st->bulk_mps = g_xhci_iface_info[hc_ctx->index][p - 1].bulk_mps;
                } else if (xhci_usbms_vidpid_fallback(st->vendor_id, st->product_id)) {
                    st->dev_class = 0x08u;
                    st->dev_subclass = 0x06u;
                    st->dev_proto = 0x50u;
                    st->bulk_in_ep = 1u;
                    st->bulk_out_ep = 2u;
                    st->bulk_mps = (st->speed >= 4u) ? 1024u : 512u;
                }
            }
        }
        if (st->present && st->address && xhci_addr_in_use(hc_ctx->index, p, st->address)) {
            st->address = 0;
        }
        if (!st->present) {
            uint32_t slot_id = g_xhci_slot_id[hc_ctx->index][p - 1];
            if (slot_id && g_xhci_rt[hc_ctx->index].ready) {
                (void)xhci_disable_slot(hc_ctx->index, slot_id);
            }
            st->address = 0;
            g_xhci_slot_id[hc_ctx->index][p - 1] = 0;
            st->slot_id = 0;
            st->vendor_id = 0;
            st->product_id = 0;
            st->dev_class = 0;
            st->dev_subclass = 0;
            st->dev_proto = 0;
            st->bulk_in_ep = 0;
            st->bulk_out_ep = 0;
            st->bulk_mps = 0;
            memset(&g_xhci_iface_info[hc_ctx->index][p - 1], 0, sizeof(xhci_iface_info_t));
        }
    }
}

static int xhci_hc_ioctl(void *ctx, uint32_t request, void *arg) {
    xhci_hc_ctx_t *hc_ctx = (xhci_hc_ctx_t*)ctx;
    xhci_info_t *hc = hc_ctx ? hc_ctx->hc : NULL;
    if (!hc || !arg) return -1;
    if (request == DEV_IOCTL_USB_HC_GET_INFO) {
        dev_usb_hc_info_t *out = (dev_usb_hc_info_t*)arg;
        memset(out, 0, sizeof(*out));
        out->index = hc_ctx->index;
        out->type = 1; /* xHCI */
        out->mmio_base = hc->mmio_base;
        out->hci_version = hc->hci_version;
        out->ports = hc->max_ports;
        out->irq_line = hc->irq_line;
        out->vendor_id = hc->vendor_id;
        out->device_id = hc->device_id;
        out->bus = hc->bus;
        out->slot = hc->slot;
        out->func = hc->func;
        return 0;
    }
    if (request == DEV_IOCTL_USB_HC_RESCAN) {
        xhci_rescan_hc(hc_ctx, 1);
        return 0;
    }
    return -1;
}

static int xhci_port_ioctl(void *ctx, uint32_t request, void *arg) {
    xhci_port_ctx_t *pc = (xhci_port_ctx_t*)ctx;
    xhci_info_t *hc = pc && pc->hc_ctx ? pc->hc_ctx->hc : NULL;
    uint32_t portsc;
    if (!pc || !hc || !arg) return -1;
    if (request == DEV_IOCTL_USB_PORT_GET_INFO) {
        dev_usb_port_info_t *out = (dev_usb_port_info_t*)arg;
        portsc = xhci_portsc_read(hc, pc->port);
        memset(out, 0, sizeof(*out));
        out->port = pc->port;
        out->connected = (portsc & XHCI_PORTSC_CCS) ? 1u : 0u;
        out->enabled = (portsc & XHCI_PORTSC_PED) ? 1u : 0u;
        out->over_current = (portsc & XHCI_PORTSC_OCA) ? 1u : 0u;
        out->reset = (portsc & XHCI_PORTSC_PR) ? 1u : 0u;
        out->speed = xhci_port_speed(portsc);
        return 0;
    }
    return -1;
}

static int xhci_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    xhci_dev_ctx_t *dc = (xhci_dev_ctx_t*)ctx;
    xhci_info_t *hc = dc && dc->hc_ctx ? dc->hc_ctx->hc : NULL;
    if (!dc || !hc || !arg) return -1;
    if (request == DEV_IOCTL_USB_DEV_GET_INFO) {
        dev_usb_dev_info_t *out = (dev_usb_dev_info_t*)arg;
        dev_usb_dev_info_t *st = &g_xhci_dev_state[dc->hc_ctx->index][dc->port - 1];
        memset(out, 0, sizeof(*out));
        out->hc_type = st->hc_type;
        out->hc_index = st->hc_index;
        out->port = st->port;
        out->present = st->present;
        out->speed = st->speed;
        out->address = st->address;
        out->slot_id = st->slot_id;
        out->vendor_id = st->vendor_id;
        out->product_id = st->product_id;
        out->dev_class = st->dev_class;
        out->dev_subclass = st->dev_subclass;
        out->dev_proto = st->dev_proto;
        out->bulk_in_ep = st->bulk_in_ep;
        out->bulk_out_ep = st->bulk_out_ep;
        out->bulk_mps = st->bulk_mps;
        return 0;
    }
    return -1;
}

static void xhci_publish_devfs(devfs_t *devfs, uint32_t hc_index, const xhci_info_t *hc) {
    char base_path[] = "/bus/usb/xhci0";
    if (!devfs || !hc || hc_index >= XHCI_MAX_CONTROLLERS) return;

    base_path[13] = (char)('0' + hc_index);
    (void)devfs_create_device_ops(devfs, base_path, 0, NULL, NULL, xhci_hc_ioctl, &g_xhci_hc_ctx[hc_index]);

    for (uint32_t p = 1; p <= hc->max_ports && p <= XHCI_MAX_PORTS; p++) {
        char port_path[] = "/bus/usb/xhci0p00";
        char dev_path[] = "/bus/usb/xhci0d00";
        g_xhci_port_ctx[hc_index][p - 1].hc_ctx = &g_xhci_hc_ctx[hc_index];
        g_xhci_port_ctx[hc_index][p - 1].port = p;
        g_xhci_dev_ctx[hc_index][p - 1].hc_ctx = &g_xhci_hc_ctx[hc_index];
        g_xhci_dev_ctx[hc_index][p - 1].port = p;
        port_path[13] = (char)('0' + hc_index);
        dev_path[13] = (char)('0' + hc_index);
        port_path[15] = (char)('0' + ((p / 10u) % 10u));
        port_path[16] = (char)('0' + (p % 10u));
        dev_path[15] = (char)('0' + ((p / 10u) % 10u));
        dev_path[16] = (char)('0' + (p % 10u));
        (void)devfs_create_device_ops(devfs, port_path, 0, NULL, NULL, xhci_port_ioctl, &g_xhci_port_ctx[hc_index][p - 1]);
        (void)devfs_create_device_ops(devfs, dev_path, 0, NULL, NULL, xhci_dev_ioctl, &g_xhci_dev_ctx[hc_index][p - 1]);
    }
}

static int xhci_init_one(const pci_device_t *d, xhci_info_t *out) {
    uint64_t mmio64;
    uint32_t mmio;
    volatile uint8_t *base;
    volatile uint32_t *usbcmd;
    volatile uint32_t *usbsts;
    volatile uint32_t *portsc;
    uint32_t hcs1;
    uint16_t cmd;
    uint8_t caplen;
    uint8_t max_ports;
    uint8_t connected = 0;

    if (!d || !out) return -1;
    if (d->header_type != 0x00) return -1;

    if (d->bar[0] & 1u) return -1;
    mmio64 = (uint64_t)(d->bar[0] & ~0xFu);
    if ((d->bar[0] & 0x6u) == 0x4u) mmio64 |= ((uint64_t)d->bar[1] << 32);
    if (mmio64 == 0 || mmio64 > 0xFFFFFFFFu) return -1;
    mmio = (uint32_t)mmio64;

    cmd = pci_cfg_read16(d->bus, d->slot, d->func, PCI_CMD_OFFSET);
    cmd |= (uint16_t)(PCI_CMD_MEM | PCI_CMD_BME);
    cmd &= (uint16_t)~PCI_CMD_IO;
    pci_cfg_write16(d->bus, d->slot, d->func, PCI_CMD_OFFSET, cmd);
    xhci_intel_route_ports(d);

    base = (volatile uint8_t*)(uintptr_t)mmio;
    xhci_legacy_handoff(base);
    caplen = *(volatile uint8_t*)(base + XHCI_CAPLENGTH);
    hcs1 = *(volatile uint32_t*)(base + XHCI_HCSPARAMS1);
    max_ports = (uint8_t)((hcs1 >> 24) & 0xFFu);

    usbcmd = (volatile uint32_t*)(base + caplen + XHCI_USBCMD);
    usbsts = (volatile uint32_t*)(base + caplen + XHCI_USBSTS);
    if (!xhci_skip_hard_reset_quirk(d)) {
        *usbcmd &= ~XHCI_USBCMD_RUNSTOP;
        (void)xhci_wait_bit(usbsts, XHCI_USBSTS_HCH, 1, 1000000);
        *usbcmd |= XHCI_USBCMD_HCRST;
        (void)xhci_wait_bit(usbcmd, XHCI_USBCMD_HCRST, 0, 2000000);
    } else {
        tty_klog("xhci: quirk skip hcrst\n");
    }

    portsc = (volatile uint32_t*)(base + caplen + XHCI_PORTSC_BASE);
    for (uint32_t i = 0; i < max_ports; i++) {
        uint32_t idx = i * (XHCI_PORT_STRIDE / 4u);
        uint32_t v = portsc[idx];
        if ((v & XHCI_PORTSC_PP) == 0) {
            portsc[idx] = v | XHCI_PORTSC_PP;
        }
    }
    for (volatile uint32_t dly = 0; dly < 500000u; dly++) { (void)dly; }
    for (uint32_t i = 0; i < max_ports; i++) {
        uint32_t v = portsc[i * (XHCI_PORT_STRIDE / 4)];
        if (v & XHCI_PORTSC_CCS) connected++;
    }
    if (connected == 0) {
        tty_klog("xhci: dbg usbcmd=");
        log_u32_hex(*usbcmd);
        tty_klog(" usbsts=");
        log_u32_hex(*usbsts);
        tty_klog("\n");
        for (uint32_t i = 0; i < max_ports && i < 8u; i++) {
            uint32_t v = portsc[i * (XHCI_PORT_STRIDE / 4)];
            tty_klog("xhci: p");
            log_u32_dec(i + 1u);
            tty_klog("=");
            log_u32_hex(v);
            tty_klog("\n");
        }
    }

    memset(out, 0, sizeof(*out));
    out->bus = d->bus;
    out->slot = d->slot;
    out->func = d->func;
    out->irq_line = d->irq_line;
    out->vendor_id = d->vendor_id;
    out->device_id = d->device_id;
    out->mmio_base = mmio;
    out->cap_length = caplen;
    out->max_ports = max_ports;
    out->connected_ports = connected;
    out->hci_version = *(volatile uint16_t*)(base + XHCI_HCIVERSION);
    return 0;
}

void xhci_init(devfs_t *devfs) {
    uint32_t occ = 0;
    uint32_t usb_other_logged = 0;
    const pci_device_t *d;

    g_xhci_count = 0;
    memset(g_xhci, 0, sizeof(g_xhci));
    memset(g_xhci_hc_ctx, 0, sizeof(g_xhci_hc_ctx));
    memset(g_xhci_port_ctx, 0, sizeof(g_xhci_port_ctx));
    memset(g_xhci_dev_ctx, 0, sizeof(g_xhci_dev_ctx));
    memset(g_xhci_dev_state, 0, sizeof(g_xhci_dev_state));
    memset(g_xhci_rt, 0, sizeof(g_xhci_rt));
    memset(g_xhci_slot_id, 0, sizeof(g_xhci_slot_id));
    memset(g_xhci_in_ctx, 0, sizeof(g_xhci_in_ctx));
    memset(g_xhci_out_ctx, 0, sizeof(g_xhci_out_ctx));
    memset(g_xhci_ep0_ring, 0, sizeof(g_xhci_ep0_ring));
    memset(g_xhci_desc_buf, 0, sizeof(g_xhci_desc_buf));
    memset(g_xhci_iface_info, 0, sizeof(g_xhci_iface_info));
    memset(g_xhci_bulk_in, 0, sizeof(g_xhci_bulk_in));
    memset(g_xhci_bulk_out, 0, sizeof(g_xhci_bulk_out));
    memset(g_xhci_intr_in, 0, sizeof(g_xhci_intr_in));

    if (devfs) {
        (void)devfs_create_dir(devfs, "/bus");
        (void)devfs_create_dir(devfs, "/bus/usb");
    }

    while ((d = pci_find_class(0x0C, 0x03, 0x30, occ)) != NULL) {
        if (g_xhci_count < XHCI_MAX_CONTROLLERS) {
            if (xhci_init_one(d, &g_xhci[g_xhci_count]) == 0) {
                xhci_info_t *xi = &g_xhci[g_xhci_count];
                int rt_ok;
                tty_klog("xhci: ");
                log_u32_hex((uint32_t)xi->bus);
                tty_klog(":");
                log_u32_hex((uint32_t)xi->slot);
                tty_klog(".");
                log_u32_dec((uint32_t)xi->func);
                tty_klog(" vid=");
                log_u32_hex((uint32_t)xi->vendor_id);
                tty_klog(" did=");
                log_u32_hex((uint32_t)xi->device_id);
                tty_klog(" mmio=");
                log_u32_hex((uint32_t)xi->mmio_base);
                tty_klog(" ports=");
                log_u32_dec((uint32_t)xi->max_ports);
                tty_klog(" up=");
                log_u32_dec((uint32_t)xi->connected_ports);
                tty_klog(" irq=");
                log_u32_dec((uint32_t)xi->irq_line);
                tty_klog("\n");

                g_xhci_hc_ctx[g_xhci_count].hc = xi;
                g_xhci_hc_ctx[g_xhci_count].index = g_xhci_count;
                rt_ok = xhci_setup_runtime(g_xhci_count, xi);
                if (rt_ok != 0) {
                    volatile uint8_t *base = (volatile uint8_t*)(uintptr_t)xi->mmio_base;
                    volatile uint32_t *usbcmd = (volatile uint32_t*)(base + xi->cap_length + XHCI_USBCMD);
                    volatile uint32_t *usbsts = (volatile uint32_t*)(base + xi->cap_length + XHCI_USBSTS);
                    volatile uint32_t *portsc = (volatile uint32_t*)(base + xi->cap_length + XHCI_PORTSC_BASE);
                    tty_klog("xhci: runtime setup failed\n");
                    for (uint32_t p = 0; p < xi->max_ports; p++) {
                        uint32_t idx = p * (XHCI_PORT_STRIDE / 4u);
                        uint32_t v = portsc[idx];
                        if ((v & XHCI_PORTSC_PP) == 0) portsc[idx] = v | XHCI_PORTSC_PP;
                    }
                    *usbcmd |= XHCI_USBCMD_RUNSTOP;
                    (void)xhci_wait_bit(usbsts, XHCI_USBSTS_HCH, 0, 1000000);
                }
                tty_klog("xhci: rescan start\n");
                xhci_rescan_hc(&g_xhci_hc_ctx[g_xhci_count], 1);
                tty_klog("xhci: rescan done\n");
                xhci_refresh_connected_ports(xi);
                if (devfs) xhci_publish_devfs(devfs, g_xhci_count, xi);
                g_xhci_count++;
            }
        }
        occ++;
    }

    if (g_xhci_count == 0) {
        tty_klog("xhci: no controller\n");
        for (uint32_t i = 0; i < pci_device_count(); i++) {
            const pci_device_t *pd = pci_get_device(i);
            if (!pd) continue;
            if (pd->class_code != 0x0Cu || pd->subclass != 0x03u) continue;
            tty_klog("usb: unsupported hc ");
            log_u32_hex((uint32_t)pd->bus);
            tty_klog(":");
            log_u32_hex((uint32_t)pd->slot);
            tty_klog(".");
            log_u32_dec((uint32_t)pd->func);
            tty_klog(" prog_if=");
            log_u32_hex((uint32_t)pd->prog_if);
            tty_klog("\n");
            usb_other_logged = 1;
        }
        if (!usb_other_logged) tty_klog("usb: no host controller in pci scan\n");
    }
}

uint32_t xhci_count(void) {
    return g_xhci_count;
}

const xhci_info_t *xhci_get(uint32_t index) {
    if (index >= g_xhci_count) return NULL;
    return &g_xhci[index];
}

int xhci_get_dev_state(uint32_t hc_index, uint32_t port, dev_usb_dev_info_t *out) {
    if (!out) return -1;
    if (hc_index >= g_xhci_count || port == 0 || port > XHCI_MAX_PORTS) return -1;
    memcpy(out, &g_xhci_dev_state[hc_index][port - 1], sizeof(*out));
    return 0;
}

int xhci_get_iface_info(uint32_t hc_index, uint32_t port, xhci_iface_info_t *out) {
    if (!out) return -1;
    if (hc_index >= g_xhci_count || port == 0 || port > XHCI_MAX_PORTS) return -1;
    memcpy(out, &g_xhci_iface_info[hc_index][port - 1], sizeof(*out));
    return out->valid ? 0 : -1;
}

void xhci_rescan_all(void) {
    for (uint32_t i = 0; i < g_xhci_count; i++) {
        xhci_rescan_hc(&g_xhci_hc_ctx[i], 1);
    }
}
