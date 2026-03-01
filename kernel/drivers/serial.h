#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8

void serial_init(uint16_t port);
bool serial_received(uint16_t port);
char serial_read_char(uint16_t port);
bool serial_transmit_empty(uint16_t port);
void serial_write_char(uint16_t port, char c);
void serial_write(uint16_t port, const char *s);
