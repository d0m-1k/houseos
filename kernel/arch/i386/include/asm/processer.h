#pragma once

#include <stdint.h>

struct struct_ptr {
    uint16_t limit;
    uintptr_t address;
} __attribute__((packed));

typedef struct registers {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} registers_t;

