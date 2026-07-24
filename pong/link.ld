ENTRY(_start)
SECTIONS
{
    . = 0x20000;
    .text : {
        *(.text.start)
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    /DISCARD/ : { *(.bss) *(.comment) *(.eh_frame) }
}
