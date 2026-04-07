bits 32
global _start
extern main
extern exit

_start:


    mov eax, [esp]
    lea ebx, [esp + 4]
    push ebx
    push eax
    call main
    add esp, 8
    push eax
    call exit
.hang:
    hlt
    jmp .hang
