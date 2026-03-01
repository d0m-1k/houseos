org 0x7C00
bits 16

_start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov ah, 0x02
    mov al, 4
    mov ch, 0x00
    mov cl, 2
    mov dh, 0x00
    mov dl, 0x80
    mov bx, 0x0000
    mov es, bx
    mov bx, 0x7E00
    int 0x13
    jc .error

    jmp 0x7E00

.error:
    mov si, disk_err_msg
    call print
.halt:
    hlt
    jmp .halt

%include "print.asm"

disk_err_msg db "MBR: Disk read error!", 0x0D, 0x0A, 0

times 446-($-$$) db 0

partion1:
    db 0x80
    db 0, 0, 0
    db 0x30
    db 0, 0, 0
    dd 1
    dd 32

partion2: times 16 db 0
partion3: times 16 db 0
partion4: times 16 db 0

dw 0xAA55
