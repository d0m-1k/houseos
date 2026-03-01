bits 32

global syscall_handler
extern do_syscall_impl

syscall_handler:
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax

    mov eax, [esp + 0]
    mov ebx, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    lea esi, [esp + 28]

    push esi
    push edx
    push ecx
    push ebx
    push eax
    call do_syscall_impl
    add esp, 20

    mov [esp + 0], eax

    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    iretd
