; entry.asm - 32-bit kernel entry at 0x10000
BITS 32

GLOBAL _start
EXTERN kernel_main

SECTION .text
_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
