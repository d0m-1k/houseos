bits 16

get_memory_map:
    pusha
    mov di, 0x5000
    xor ebx, ebx
    mov edx, 0x534D4150 ; 'SMAP'
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .error
    
.loop:
    add di, 24
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .done
    test ebx, ebx
    jnz .loop
    
.done:
    popa
    ret
    
.error:
    mov si, mmap_error_msg
    call print
    ret

mmap_error_msg db "Memory map error!", 0x0D, 0x0A, 0