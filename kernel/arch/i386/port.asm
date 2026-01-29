section .text

global inb
inb:
    push edx
    mov dx, [esp+8]
    xor eax, eax
    in al, dx
    pop edx
    ret

global inw
inw:
    push edx
    mov dx, [esp+8]
    in ax, dx
    pop edx
    ret

global indw
indw:
    push edx
    mov dx, [esp+8]
    in eax, dx
    pop edx
    ret

global outb
outb:
    push edx
    push eax
    mov dx, [esp+12]
    mov al, [esp+16]
    out dx, al
    pop eax
    pop edx
    ret

global outw
outw:
    push edx
    push eax
    mov dx, [esp+12]
    mov ax, [esp+16]
    out dx, ax
    pop eax
    pop edx
    ret

global outdw
outdw:
    push edx
    push eax
    mov dx, [esp+12]
    mov eax, [esp+16]
    out dx, eax
    pop eax
    pop edx
    ret
