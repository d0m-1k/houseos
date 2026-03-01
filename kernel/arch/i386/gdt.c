#include <asm/gdt.h>

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[GDT_COUNT];
static gdt_ptr_t gp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[num].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

void gdt_set_tss(uint32_t base, uint32_t limit) {
    gdt_set_gate(GDT_TSS, base, limit, 0x89, 0x40);
}

void gdt_init(void) {
    gp.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_COUNT - 1);
    gp.base = (uint32_t)&gdt;

    gdt_set_gate(GDT_NULL, 0, 0, 0, 0);
    gdt_set_gate(GDT_KERNEL_CODE, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(GDT_KERNEL_DATA, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(GDT_USER_CODE, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(GDT_USER_DATA, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    gdt_set_gate(GDT_TSS, 0, 0, 0, 0);

    __asm__ __volatile__("lgdt (%0)" : : "r"(&gp));
    __asm__ __volatile__(
        "movw %0, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "ljmp %1, $1f\n"
        "1:\n"
        :
        : "i"(GDT_KERNEL_DATA * 8), "i"(GDT_KERNEL_CODE * 8)
        : "ax", "memory"
    );
}
