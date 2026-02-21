global context_switch
context_switch:
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi
    mov eax, [esp + 32]
    mov [eax], esp
    mov eax, [esp + 36]
    mov esp, eax
    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    ret
