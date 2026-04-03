bits 16

vga_set_mode:
    mov al, [vga_mode_value]
    cmp al, 0x02
    je .apply
    cmp al, 0x03
    je .apply
    mov al, 0x03
.apply:
    mov ah, 0x00
    int 0x10
    ret
