#pragma once

#include <stdint.h>

enum {
    GDT_NULL = 0,
    GDT_KERNEL_CODE = 1,
    GDT_KERNEL_DATA = 2,
    GDT_USER_CODE = 3,
    GDT_USER_DATA = 4,
    GDT_TSS = 5,
    GDT_COUNT = 6
};

void gdt_init(void);
void gdt_set_tss(uint32_t base, uint32_t limit);
