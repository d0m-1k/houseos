#pragma once

#include <stdint.h>

#define IDT_ENTRIES 256

struct idt_entry {
	uint16_t base_low;
	uint16_t selector;
	uint8_t zero;
	uint8_t flags;
	uint16_t base_high;
} __attribute__((packed));

struct interrupt_frame {
    uint32_t ip;
    uint32_t cs;
    uint32_t flags;
    uint32_t sp;
    uint32_t ss;
} __attribute__((packed));

void idt_init();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_load(uint32_t idt_ptr);
void idt_handler(uint8_t num, uint32_t err_code);
void idt_set_handler(uint32_t addr);
void pic_init();

extern struct idt_entry idt[IDT_ENTRIES];
extern struct struct_ptr idtp;