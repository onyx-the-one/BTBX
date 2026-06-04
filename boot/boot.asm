; BTBX boot.asm
; GRUB Multiboot v1 header + 32-bit protected mode entry
; Build: nasm -f elf32 boot.asm -o boot.o

MBOOT_MAGIC  equ 0x1BADB002
MBOOT_FLAGS  equ 0x00000003
MBOOT_CHECK  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECK

section .bss
align 16
stack_bottom:
    resb 65536          ; 64 KB stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    cld
    mov  esp, stack_top
    push ebx            ; multiboot info*
    push eax            ; magic
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
