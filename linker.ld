OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text   ALIGN(16) : { *(.text)    }
    .rodata ALIGN(16) : { *(.rodata*) }
    .data   ALIGN(16) : { *(.data)    }
    _bss_start = .;
    .bss    ALIGN(16) : { *(COMMON); *(.bss); *(.lowbss); }
    _bss_end = .;
}
