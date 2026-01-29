enable_a20:
    in al, 0x92
    test al, 2
    jnz .a20_done    ; Уже включена
    or al, 2
    and al, 0xFE     ; Не сбрасываем процессор
    out 0x92, al
.a20_done:
    ret