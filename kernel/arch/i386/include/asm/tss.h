#pragma once

#include <stdint.h>

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint16_t es; uint16_t _r_es;
    uint16_t cs; uint16_t _r_cs;
    uint16_t ss; uint16_t _r_ss;
    uint16_t ds; uint16_t _r_ds;
    uint16_t fs; uint16_t _r_fs;
    uint16_t gs; uint16_t _r_gs;
    uint16_t ldt; uint16_t _r_ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

extern tss_entry_t tss;

void tss_init(uint32_t kernel_stack_top);
