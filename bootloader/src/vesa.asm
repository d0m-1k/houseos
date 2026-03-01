vesa_load:
    push es
    mov ax, 0
    mov es, ax
    mov ax, 0x4F00
    mov di, vesa_info_buffer
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vesa_error

    push es
    push ds
    mov ax, ds
    mov es, ax
    mov si, vesa_info_buffer
    mov di, 0x9000
    mov cx, 512
    rep movsb
    pop ds
    pop es

    push es
    mov ax, 0
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, mode_info_buffer
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .mode_error

    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .switch_error

    push es
    mov ax, ds
    mov es, ax
    mov si, mode_info_buffer
    mov di, 0x9100
    mov cx, 256
    rep movsb
    pop es
    
    ret

.vesa_error:
    mov si, vesa_err_msg
    call print
    cli
    hlt
.mode_error:
    mov si, mode_err_msg
    call print
    cli
    hlt
.switch_error:
    mov si, switch_err_msg
    call print
    cli
    hlt

vesa_err_msg: db "VESA error!", 0
mode_err_msg: db "Mode error!", 0
switch_err_msg: db "Switch error!", 0

vesa_info_buffer: times 512 db 0
mode_info_buffer: times 256 db 0
