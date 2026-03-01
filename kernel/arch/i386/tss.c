#include <asm/tss.h>
#include <asm/gdt.h>
#include <string.h>

tss_entry_t tss;

void tss_init(uint32_t kernel_stack_top) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = GDT_KERNEL_DATA * 8;
    tss.esp0 = kernel_stack_top;
    tss.iomap_base = sizeof(tss);

    gdt_set_tss((uint32_t)&tss, sizeof(tss_entry_t) - 1);
    __asm__ __volatile__("ltr %%ax" : : "a"(GDT_TSS * 8));
}
