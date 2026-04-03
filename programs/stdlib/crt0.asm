bits 32
global _start
extern main
extern exit

_start:
    ; Initial user stack layout provided by kernel:
    ;   [esp + 0]  = argc
    ;   [esp + 4]  = argv[0]
    ;   ...
    ;   [esp + 4 + 4*argc] = NULL
    mov eax, [esp]       ; argc
    lea ebx, [esp + 4]   ; argv
    push ebx
    push eax
    call main
    add esp, 8
    push eax
    call exit
.hang:
    hlt
    jmp .hang
