ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text : {
        */boot.o(.text)
        *(.text)
    }
    
    .data : {
        *(.data)
    }
    
    .rodata : {
        *(.rodata)
    }
    
    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    
    _end = .;
}