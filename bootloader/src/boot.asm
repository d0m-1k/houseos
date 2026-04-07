org 0x7E00
bits 16

CFG_MAGIC               equ 0x47464348
CFG_KERNEL_SIZE         equ 8
CFG_KERNEL_LBA          equ 12
CFG_KERNEL_ADDR         equ 16
CFG_INITRAMFS_SIZE      equ 20
CFG_INITRAMFS_LBA       equ 24
CFG_INITRAMFS_ADDR      equ 28
CFG_MEMMAP_ADDR         equ 32
CFG_VIDEO_OUTPUT        equ 36
CFG_VESA_MODE           equ 38
CFG_VGA_MODE            equ 40
CFG_VESA_INFO_ADDR      equ 44
CFG_VESA_MODE_INFO_ADDR equ 48
CFG_VESA_MODES_ADDR     equ 52
CFG_STAGE2_LBA          equ 56
CFG_STAGE2_SECTORS      equ 60
CFG_FLAGS               equ 64
CFG_ROOTFS_LBA          equ 68
CFG_ROOTFS_SIZE         equ 72

CFG_FLAG_DEBUG          equ 1
CFG_FLAG_DYNAMIC_PARAMS equ 2
LOAD_BOUNCE_ADDR        equ 0x3000

_start:
    jmp 0x0000:.cs0
.cs0:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov esp, 0x00007C00

    mov [boot_drive], dl

    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .lba_error
    cmp bx, 0xAA55
    jne .lba_error


    mov word [dap_sectors], 1
    mov word [dap_offset], 0x0600
    mov word [dap_segment], 0x0000
    mov dword [dap_start_lba], 1
    mov dword [dap_start_lba+4], 0
    call read_lba
    jc .cfg_error

    mov eax, [cfg_base + 0]
    cmp eax, CFG_MAGIC
    jne .cfg_error

    mov eax, [cfg_base + CFG_FLAGS]
    mov [boot_flags], eax
    test dword [boot_flags], CFG_FLAG_DEBUG
    jz .no_dbg
    mov byte [debug_enabled], 1
    call serial_init
.no_dbg:

    mov eax, [cfg_base + CFG_MEMMAP_ADDR]
    mov [memmap_addr], eax
    call linear_to_seg_off
    mov [memmap_off], ax
    mov [memmap_seg], dx

    mov eax, [cfg_base + CFG_VESA_INFO_ADDR]
    mov [vesa_info_addr], eax
    call linear_to_seg_off
    mov [vesa_info_off], ax
    mov [vesa_info_seg], dx

    mov eax, [cfg_base + CFG_VESA_MODE_INFO_ADDR]
    mov [vesa_mode_info_addr], eax
    call linear_to_seg_off
    mov [vesa_mode_info_off], ax
    mov [vesa_mode_info_seg], dx

    test dword [boot_flags], CFG_FLAG_DYNAMIC_PARAMS
    jz .keep_static_video
    mov ax, [cfg_base + CFG_VIDEO_OUTPUT]
    mov [video_output_value], ax
    mov ax, [cfg_base + CFG_VESA_MODE]
    mov [vesa_mode_value], ax
    mov ax, [cfg_base + CFG_VGA_MODE]
    mov [vga_mode_value], ax
.keep_static_video:

    mov dword [pm_entry_addr], 0x00010000

    mov si, msg_a20
    call dbg_print
    call enable_a20

    mov si, msg_load_kernel
    call dbg_print
    mov eax, [cfg_base + CFG_KERNEL_LBA]
    mov ebx, 0x00010000
    mov ecx, [cfg_base + CFG_KERNEL_SIZE]
    call load_blob
    jc .disk_kernel_error

    mov si, msg_load_initramfs
    call dbg_print
    mov eax, [cfg_base + CFG_INITRAMFS_LBA]
    mov ebx, [cfg_base + CFG_INITRAMFS_ADDR]
    mov ecx, [cfg_base + CFG_INITRAMFS_SIZE]
    call load_blob
    jc .disk_initramfs_error

    mov si, msg_memmap
    call dbg_print
    call get_memory_map

    mov si, msg_video
    call dbg_print
    cmp word [video_output_value], 1
    je .video_vga
    call vesa_load
    jnc .video_ok
.video_vga:
    call vga_set_mode
.video_ok:

    mov si, msg_pm
    call dbg_print
    call switch_to_pm
    jmp .halt

.cfg_error:
    mov si, cfg_err_msg
    call print
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

.halt:
    cli
    hlt
    jmp .halt


load_blob:
    pushad
    mov [load_lba], eax
    mov [load_addr], ebx
    mov eax, ecx
    add eax, 511
    shr eax, 9
    mov [load_sectors_left], eax

.loop:
    mov eax, [load_sectors_left]
    test eax, eax
    jz .done
    cmp eax, 8
    jbe .chunk_ok
    mov eax, 8
.chunk_ok:
    mov [load_chunk], ax
    mov word [dap_sectors], ax

    mov eax, [load_addr]
    cmp eax, 0x00100000
    jae .bounce_read
    call linear_to_seg_off
    mov [dap_offset], ax
    mov [dap_segment], dx
    jmp .do_read

.bounce_read:
    mov word [dap_offset], LOAD_BOUNCE_ADDR
    mov word [dap_segment], 0x0000

.do_read:
    mov eax, [load_lba]
    mov [dap_start_lba], eax
    mov dword [dap_start_lba+4], 0

    call read_lba
    jc .error

    mov eax, [load_addr]
    cmp eax, 0x00100000
    jb .advance

    xor esi, esi
    mov si, LOAD_BOUNCE_ADDR
    mov edi, [load_addr]
    movzx ecx, word [load_chunk]
    shl ecx, 7
.copy_dwords:
    mov eax, [esi]
    mov [edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_dwords

.advance:
    movzx eax, word [load_chunk]
    mov edx, eax
    shl eax, 9
    add [load_addr], eax

    mov eax, [load_lba]
    add eax, edx
    mov [load_lba], eax

    mov eax, [load_sectors_left]
    sub eax, edx
    mov [load_sectors_left], eax
    jmp .loop

.done:
    popad
    clc
    ret
.error:
    popad
    stc
    ret


linear_to_seg_off:
    push bx
    mov ebx, eax
    mov ax, bx
    and ax, 0x000F
    shr ebx, 4
    mov dx, bx
    pop bx
    ret

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

dbg_print:
    cmp byte [debug_enabled], 0
    je .done
    call print
    call serial_print
.done:
    ret

serial_init:
    push dx
    mov dx, 0x3F9
    mov al, 0x00
    out dx, al

    mov dx, 0x3FB
    mov al, 0x80
    out dx, al

    mov dx, 0x3F8
    mov al, 0x03
    out dx, al

    mov dx, 0x3F9
    mov al, 0x00
    out dx, al

    mov dx, 0x3FB
    mov al, 0x03
    out dx, al

    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al

    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    pop dx
    ret

serial_putc:
    push ax
    push dx
    mov ah, al
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

serial_print:
    pusha
.loop:
    lodsb
    cmp al, 0
    je .done
    call serial_putc
    jmp .loop
.done:
    popa
    ret

%include "print.asm"
%include "gdt.asm"
%include "memory.asm"
%include "vesa.asm"
%include "vga.asm"
%include "a20.asm"
%include "modes.asm"

cfg_base             equ 0x0600

dap:
    db 0x10
    db 0x00
dap_sectors:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_start_lba:
    dd 0
    dd 0

boot_drive:          db 0
debug_enabled:       db 0
boot_flags:          dd 0
video_output_value:  dw 1
vga_mode_value:      dw 0x03

memmap_addr:         dd 0x5000
memmap_seg:          dw 0
memmap_off:          dw 0

vesa_mode_value:     dw 0x118
vesa_info_addr:      dd 0x9000
vesa_mode_info_addr: dd 0x9100
vesa_info_seg:       dw 0
vesa_info_off:       dw 0
vesa_mode_info_seg:  dw 0
vesa_mode_info_off:  dw 0

load_lba:            dd 0
load_addr:           dd 0
load_sectors_left:   dd 0
load_chunk:          dw 0

pm_entry_addr:       dd 0x10000

lba_err_msg:               db "LBA not supported!", 0x0D, 0x0A, 0
cfg_err_msg:               db "Boot config read error", 0x0D, 0x0A, 0
disk_initramfs_err_msg:    db "InitRamFS read error! Code: ", 0
disk_kernel_err_msg:       db "Kernel read error! Code: ", 0

msg_load_kernel:           db "ST2: load kernel", 0x0D, 0x0A, 0
msg_load_initramfs:        db "ST2: load initramfs", 0x0D, 0x0A, 0
msg_memmap:                db "ST2: memorymap", 0x0D, 0x0A, 0
msg_video:                 db "ST2: video", 0x0D, 0x0A, 0
msg_a20:                   db "ST2: a20", 0x0D, 0x0A, 0
msg_pm:                    db "ST2: pm", 0x0D, 0x0A, 0


vesa_info_buffer:          times 512 db 0
mode_info_buffer:          times 256 db 0
