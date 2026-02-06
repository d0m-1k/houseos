#pragma once

#include <stdint.h>

struct struct_ptr {
	uint16_t limit;
	uintptr_t base;
} __attribute__((packed));

void hlt(void);
void sti(void);
void cli(void);