#include <asm/idt.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/syscall.h>
#include <asm/port.h>
#include <asm/processor.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <asm/gdt.h>

struct idt_entry idt[IDT_ENTRIES];
struct struct_ptr idtp;

uint32_t handler_address = 0;

void idt_set_handler(uint32_t addr) {
    handler_address = addr;
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
}

void idt_load(uint32_t idt_ptr) {
    __asm__ __volatile__("lidtl (%0)" : : "r"(idt_ptr));
}

#define isr(n) void __attribute__((interrupt)) i##n(struct interrupt_frame* frame) { (void)(frame); idt_handler(n, 0); }
#define isr_err(n) void __attribute__((interrupt)) i##n(struct interrupt_frame* frame, uint32_t err) { (void)(frame); idt_handler(n, err); }

isr(0) isr(1) isr(2) isr(3) isr(4) isr(5) isr(6) isr(7)
isr_err(8) isr(9) isr_err(10) isr_err(11) isr_err(12) isr_err(13) isr_err(14)
isr(15) isr(16) isr_err(17) isr(18) isr(19)
isr(32) isr(33) isr(34) isr(35) isr(36) isr(37) isr(38) isr(39)
isr(40) isr(41) isr(42) isr(43) isr(44) isr(45) isr(46) isr(47)

#undef isr
#undef isr_err

void __attribute__((interrupt)) default_handler(struct interrupt_frame* frame) {
    (void)(frame);
    while (1) __asm__ __volatile__("hlt");
}

void pic_init() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0b11111000);
    outb(0xA1, 0b11101111);
}

void idt_handler(uint8_t num, uint32_t err_code) {
    if (num == 32) {
        outb(0x20, 0x20);
        timer_handler();
        return;
    } else if (num == 33) {
        keyboard_handler();
    } else if (num == 44) {
        mouse_handler();
    } else if (handler_address != 0) {
        void (*handler)(uint8_t, uint32_t) = (void (*)(uint8_t, uint32_t)) handler_address;
        handler(num, err_code);
    } else {
        if (num < 32) {
            while (1) __asm__ __volatile__("hlt");
        }
    }

    if (num >= 32 && num < 40) {
        outb(0x20, 0x20);
    } else if (num >= 40 && num < 48) {
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}

void idt_init() {
    pic_init();

    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, (uint32_t)default_handler, 0x08, 0x8E);
    }

    #define set(n) idt_set_gate(n, (uint32_t)i##n, GDT_KERNEL_CODE * 8, 0x8E);
    set(0) set(1) set(2) set(3) set(4) set(5) set(6) set(7)
    set(8) set(9) set(10) set(11) set(12) set(13) set(14)
    set(15) set(16) set(17) set(18) set(19)
    set(32) set(33) set(34) set(35) set(36) set(37) set(38) set(39)
    set(40) set(41) set(42) set(43) set(44) set(45) set(46) set(47)
    #undef set

    idt_set_gate(0x80, (uint32_t)syscall_handler, GDT_KERNEL_CODE * 8, 0xEE);

    idt_load((uint32_t)&idtp);
}
