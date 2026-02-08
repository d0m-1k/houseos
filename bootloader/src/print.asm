print:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

print_hex_byte:
    pusha
    mov cx, 2
.hex_loop:
    rol al, 4
    mov bl, al
    and bl, 0x0F
    cmp bl, 10
    jl .is_digit
    add bl, 'A' - 10 - '0'
.is_digit:
    add bl, '0'
    mov ah, 0x0E
    mov al, bl
    int 0x10
    loop .hex_loop

    mov al, ' '
    int 0x10
    popa
    ret