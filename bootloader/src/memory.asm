bits 16

get_memory_map:
    pusha
    push es
    
    xor ax, ax
    mov es, ax
    mov di, 0x5000
    
    xor ebx, ebx
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    int 0x15
    jc .error
    cmp eax, 0x534D4150
    jne .error
    
    add di, 20
    
.next_entry:
    test ebx, ebx
    jz .done
    
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    int 0x15
    jc .done
    
    add di, 20
    jmp .next_entry

.done:
    xor eax, eax
    mov [es:di], eax
    mov [es:di+4], eax
    mov [es:di+8], eax
    mov [es:di+12], eax
    mov [es:di+16], eax
    
    pop es
    popa
    ret

.error:
    xor eax, eax
    mov [es:di], eax
    mov [es:di+4], eax
    mov [es:di+8], eax
    mov [es:di+12], eax
    mov [es:di+16], eax
    
    pop es
    popa
    ret
