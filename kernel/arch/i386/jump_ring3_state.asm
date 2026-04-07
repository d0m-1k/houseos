bits 32

global jump_to_ring3_state

; cdecl:
; [esp+4]  = user_eip
; [esp+8]  = user_esp
; [esp+12] = user_eflags
; [esp+16] = eax
; [esp+20] = ebx
; [esp+24] = ecx
; [esp+28] = edx
; [esp+32] = esi
; [esp+36] = edi
; [esp+40] = ebp

jump_to_ring3_state:
    cli

    mov edx, [esp + 4]
    mov esi, [esp + 8]
    mov edi, [esp + 12]

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23
    push esi
    push edi
    push dword 0x1B
    push edx

    mov eax, [esp + 36]
    mov ebx, [esp + 40]
    mov ecx, [esp + 44]
    mov edx, [esp + 48]
    mov esi, [esp + 52]
    mov edi, [esp + 56]
    mov ebp, [esp + 60]

    iretd
