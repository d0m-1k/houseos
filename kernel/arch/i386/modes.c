#include <asm/modes.h>
#include <asm/processor.h>
#include <asm/gdt.h>

void jump_to_ring3(uint32_t user_eip, uint32_t user_esp, uint32_t user_eflags) {
    cli();

    __asm__ __volatile__(
        "movw %w0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl %0\n"
        "pushl %1\n"
        "pushl %2\n"
        "pushl %3\n"
        "pushl %4\n"
        "iret\n"
        :
        : "r"((GDT_USER_DATA * 8) | 3), "r"(user_esp), "r"(user_eflags),
          "r"((GDT_USER_CODE * 8) | 3), "r"(user_eip)
        : "ax", "memory"
    );
}
