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
    mov al, 4      ; Количество секторов
    mov ch, 0x00
    mov cl, 2      ; Начинаем со 2-го сектора
    mov dh, 0x00
    mov dl, 0x80
    mov bx, 0x0000 ; ES:BX = 0x0000:0x7E00
    mov es, bx
    mov bx, 0x7E00
    int 0x13
    jc .error

    jmp 0x7E00

.error:
    mov si, disk_err_msg
    call print
.end:
    hlt
    jmp .end

%include "print.asm"

disk_err_msg db "MBR: Disk read error!", 0x0D, 0x0A, 0

times 446-($-$$) db 0

; Таблица разделов
partion1:
    db 0x80            ; Активный раздел
    db 0, 0, 0         ; CHS начала
    db 0x30            ; Тип (FAT32 LBA)
    db 0, 0, 0         ; CHS конца
    dd 1               ; LBA начала
    dd 32              ; Количество секторов

partion2: times 16 db 0
partion3: times 16 db 0
partion4: times 16 db 0

dw 0xAA55