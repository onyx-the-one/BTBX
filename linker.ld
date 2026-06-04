/* linker.ld – bare-metal 32-bit ELF, loaded at 1 MB by GRUB */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
	    . = 0x100000;

    .text : {
	        *(.multiboot)
        *(.text)
    }

    .rodata : { *(.rodata*) }

    .data   : { *(.data) }

    .bss    : {
	        *(COMMON)
        *(.bss)
    }
}
