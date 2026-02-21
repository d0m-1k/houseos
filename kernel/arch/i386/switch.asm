global context_switch
context_switch:
    pushfd                 ; сохраняем EFLAGS
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    mov eax, [esp + 36]    ; адрес prev->esp (после 8 push'ов + адрес возврата)
    mov [eax], esp

    mov eax, [esp + 40]    ; адрес next->esp
    mov esp, eax

    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    popfd                  ; восстанавливаем EFLAGS
    ret