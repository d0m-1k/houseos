org 0x7E00
bits 16

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .lba_error
    cmp bx, 0xAA55
    jne .lba_error

    mov dword [dap_start_lba], 6
    mov word [dap_offset], 0x0000
    mov word [dap_segment], 0x4000
    mov word [dap_sectors], 64
    call read_lba
    jc .disk_initramfs_error

    mov dword [dap_start_lba], 70
    mov word [dap_offset], 0x0000
    mov word [dap_segment], 0x1000
    mov word [dap_sectors], 128
    call read_lba
    jc .disk_kernel_error
    
    call get_memory_map
    call vesa_load
    call enable_a20
    
    call switch_to_pm
    jmp .halt

.lba_error:
    mov si, lba_err_msg
    call print
    jmp .halt

.disk_initramfs_error:
    mov si, disk_initramfs_err_msg
    call print
    mov al, ah
    call print_hex_byte
    jmp .halt

.disk_kernel_error:
    mov si, disk_kernel_err_msg
    call print
    mov al, ah
    call print_hex_byte
    jmp .halt

.kernel_corrupt:
    mov si, kernel_corrupt_msg
    call print
    jmp .halt

.halt:
    cli
    hlt
    jmp .halt

read_lba:
    pusha
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error
    popa
    ret
.error:
    popa
    stc
    ret

%include "print.asm"
%include "gdt.asm"
%include "memory.asm"
%include "vesa.asm"
%include "a20.asm"
%include "modes.asm"

dap:
    db 0x10
    db 0x00
dap_sectors:
    dw 0x0000
dap_offset:
    dw 0x0000
dap_segment:
    dw 0x0000
dap_start_lba:
    dd 0x00000000
    dd 0x00000000

boot_drive: db 0x00

lba_err_msg: db "LBA not supported!", 0x0D, 0x0A, 0
disk_initramfs_err_msg: db "InitRamFS read error! Code: ", 0
disk_kernel_err_msg: db "Kernel read error! Code: ", 0
initramfs_corrupt_msg: db "InitRamFS corrupted!", 0x0D, 0x0A, 0
kernel_corrupt_msg: db "Kernel corrupted!", 0x0D, 0x0A, 0