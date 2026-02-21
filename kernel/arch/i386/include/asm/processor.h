#pragma once

#include <stdint.h>

struct struct_ptr {
	uint16_t limit;
	uintptr_t base;
} __attribute__((packed));

#define hlt() __asm__ __volatile__("hlt")
#define sti() __asm__ __volatile__("sti")
#define cli() __asm__ __volatile__("cli")