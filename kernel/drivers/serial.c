#include <drivers/serial.h>
#include <asm/port.h>

void serial_init(uint16_t port) {
    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, 0x03);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0B);
}

bool serial_received(uint16_t port) {
    return (inb(port + 5) & 1) != 0;
}

char serial_read_char(uint16_t port) {
    while (!serial_received(port));
    return (char)inb(port);
}

bool serial_transmit_empty(uint16_t port) {
    return (inb(port + 5) & 0x20) != 0;
}

void serial_write_char(uint16_t port, char c) {
    while (!serial_transmit_empty(port));
    outb(port, (uint8_t)c);
}

void serial_write(uint16_t port, const char *s) {
    if (!s) return;
    while (*s) serial_write_char(port, *s++);
}
