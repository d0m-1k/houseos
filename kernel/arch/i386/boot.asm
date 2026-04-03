bits 32
global _start
global kernel_stack_bottom
global kernel_stack_top
extern kmain

extern __bss_start
extern __bss_end

KERNEL_BOOT_STACK_TOP equ 0x00070000

_start:
    cli

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, KERNEL_BOOT_STACK_TOP
    
    call kmain
    
.end:
    hlt
    jmp .end

section .bss
align 16
kernel_stack_bottom:
    resb 16384
kernel_stack_top:
