org 0x7E00
bits 16

_start:
    cli

    mov ah, 0x02
    mov al, 64     ; Количество секторов
    mov ch, 0x00
    mov cl, 6      ; Сектор 6
    mov dh, 0x00   
    mov dl, 0x80
    mov bx, 0x1000 ; ES:BX = 0x1000:0x0000
    mov es, bx
    mov bx, 0x0000
    int 0x13
    jc .disk_error

    call get_memory_map

    mov [boot_drive], dl
    call enable_a20
    
    call switch_to_pm
    
.disk_error:
    mov si, disk_err_msg
    call print
.end:
    hlt
    jmp .end

%include "print.asm"
%include "gdt.asm"
%include "memory.asm"
%include "a20.asm"
%include "modes.asm"

boot_drive db 0
disk_err_msg db "ST2: Disk read error!", 0x0D, 0x0A, 0