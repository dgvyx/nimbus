ENTRY(_start)
SECTIONS {
    . = 0x100000;
    .text : {
        KEEP(*(.multiboot))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        *(COMMON)
        *(.bss*)
    }
}
