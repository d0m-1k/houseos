global context_switch
context_switch:
    pushfd
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    mov eax, [esp + 36]
    mov edx, [esp + 40]
    mov ecx, [esp + 44]
    mov [eax], esp
    mov cr3, ecx
    mov esp, edx

    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    popfd
    ret
