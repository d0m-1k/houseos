bits 16
switch_to_pm:
    cli
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    lgdt [gdt_descriptor]
    
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    
    jmp CODE_SEG:init_pm

bits 32
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    mov ebp, 0x70000
    mov esp, ebp


    lea edi, [pm_idt]
    mov ecx, 14
    mov eax, pm_fault_halt
    mov ebx, eax
    and eax, 0x0000FFFF
    shl ebx, 16
    or eax, (CODE_SEG << 16)
    mov edx, pm_fault_halt
    and edx, 0xFFFF0000
    or edx, 0x00008E00
.idt_fill:
    mov [edi], eax
    mov [edi + 4], edx
    add edi, 8
    dec ecx
    jnz .idt_fill
    lidt [pm_idtr]
    cli
    
    mov eax, 0x00010000
    jmp eax

pm_fault_halt:
    cli
.halt:
    hlt
    jmp .halt

align 8
pm_idt:
    times (14 * 8) db 0
pm_idtr:
    dw (14 * 8) - 1
    dd pm_idt
