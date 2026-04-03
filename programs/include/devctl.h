#pragma once

#include <stdint.h>

enum {
    DEV_IOCTL_VESA_GET_INFO = 0x1000,
    DEV_IOCTL_VGA_GET_INFO = 0x1001,
    DEV_IOCTL_TTY_GET_INFO = 0x1100,
    DEV_IOCTL_TTY_SET_ACTIVE = 0x1101,
    DEV_IOCTL_TTY_GET_ACTIVE = 0x1102,
    DEV_IOCTL_TTY_SET_FG_PID = 0x1103,
    DEV_IOCTL_TTY_GET_FG_PID = 0x1104,
    DEV_IOCTL_KBD_GET_INFO = 0x1200,
    DEV_IOCTL_KBD_GET_EVENT = 0x1201,
    DEV_IOCTL_KBD_SET_LAYOUT = 0x1202,
    DEV_IOCTL_MOUSE_GET_INFO = 0x1300,
    DEV_IOCTL_POWER_REBOOT = 0x1400,
    DEV_IOCTL_POWER_POWEROFF = 0x1401,
    DEV_IOCTL_POWER_GET_CAD_MODE = 0x1402,
    DEV_IOCTL_POWER_SET_CAD_MODE = 0x1403,
    DEV_IOCTL_DISK_GET_INFO = 0x1500,
    DEV_IOCTL_DISK_READ = 0x1501,
    DEV_IOCTL_DISK_WRITE = 0x1502,
    DEV_IOCTL_BOOTLOADER_SET = 0x1600,
    DEV_IOCTL_BOOTLOADER_GET = 0x1601,
    DEV_IOCTL_BOOTLOADER_GET_MODES = 0x1602,
    DEV_IOCTL_PCI_GET_INFO = 0x1700,
    DEV_IOCTL_PCI_CFG_READ32 = 0x1701,
    DEV_IOCTL_USB_HC_GET_INFO = 0x1800,
    DEV_IOCTL_USB_PORT_GET_INFO = 0x1801,
    DEV_IOCTL_USB_HC_RESCAN = 0x1802,
    DEV_IOCTL_USB_DEV_GET_INFO = 0x1803,
    DEV_IOCTL_PTY_ALLOC = 0x1900,
    DEV_IOCTL_PTY_FREE = 0x1901,
    DEV_IOCTL_PTY_RESET = 0x1902,
    DEV_IOCTL_PTY_GET_READABLE = 0x1903,
};

enum {
    DEV_TTY_KIND_VESA = 0,
    DEV_TTY_KIND_SERIAL = 1,
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t address;
    uint32_t size;
} dev_fb_info_t;

typedef struct {
    uint32_t cols;
    uint32_t rows;
    uint32_t mode;
    uint32_t address;
    uint32_t size;
} dev_vga_info_t;

typedef struct {
    uint32_t kind;
    uint32_t index;
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_x;
    uint32_t cursor_y;
} dev_tty_info_t;

typedef struct {
    uint32_t layout;
    uint32_t caps_lock;
    uint32_t num_lock;
    uint32_t scroll_lock;
} dev_keyboard_info_t;

typedef struct {
    uint8_t scancode;
    int8_t ascii;
    uint8_t pressed;
    uint8_t shift;
    uint8_t ctrl;
    uint8_t alt;
    uint8_t caps;
    uint8_t reserved;
} dev_keyboard_event_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t buttons;
} dev_mouse_info_t;

typedef struct {
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t flags;
} dev_disk_info_t;

typedef struct {
    uint32_t lba;
    uint32_t count;
    uint32_t buffer;
} dev_disk_rw_t;

typedef struct {
    char prop_name[32];
    uint32_t value;
} dev_bootloader_set_t;

typedef struct {
    uint16_t id;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t reserved;
} dev_bootloader_mode_t;

typedef struct {
    uint32_t count;
    dev_bootloader_mode_t modes[256];
} dev_bootloader_modes_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq_line;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar[6];
} dev_pci_info_t;

typedef struct {
    uint32_t offset;
    uint32_t value;
} dev_pci_cfg_rw_t;

typedef struct {
    uint32_t index;
    char master_path[32];
    char slave_path[32];
} dev_pty_alloc_t;

typedef struct {
    uint32_t index;
    uint32_t type;
    uint32_t mmio_base;
    uint32_t hci_version;
    uint32_t ports;
    uint32_t irq_line;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t reserved;
} dev_usb_hc_info_t;

typedef struct {
    uint32_t port;
    uint32_t connected;
    uint32_t enabled;
    uint32_t speed;
    uint32_t reset;
    uint32_t over_current;
} dev_usb_port_info_t;

typedef struct {
    uint32_t hc_type;
    uint32_t hc_index;
    uint32_t port;
    uint32_t present;
    uint32_t speed;
    uint32_t address;
    uint32_t slot_id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t dev_class;
    uint8_t dev_subclass;
    uint8_t dev_proto;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint16_t bulk_mps;
} dev_usb_dev_info_t;
