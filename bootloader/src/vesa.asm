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
    mov cx, [vesa_mode_value]
    mov di, mode_info_buffer
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .mode_error

    mov ax, 0x4F02
    mov bx, [vesa_mode_value]
    or bx, 0x4000
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
    
    clc
    ret

.vesa_error:
    mov si, vesa_err_msg
    call print
    stc
    ret
.mode_error:
    mov si, mode_err_msg
    call print
    stc
    ret
.switch_error:
    mov si, switch_err_msg
    call print
    stc
    ret

vesa_err_msg: db "VESA error!", 0
mode_err_msg: db "Mode error!", 0
switch_err_msg: db "Switch error!", 0
