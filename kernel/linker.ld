ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    _bss_start = .;
    .bss    : { *(.bss*) *(COMMON) }
    _bss_end = .;
}
