; stage1.asm - 512-byte MBR
; Prints S1, loads stage2 (sectors 2-5) to 0x7E00, jumps there.

BITS 16
ORG 0x7C00

    jmp 0x0000:start

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive], dl

    mov al, 'S'
    call putc
    mov al, '1'
    call putc

    mov ax, 0x07E0
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 4
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [drive]
    int 0x13
    jc disk_err

    mov al, '>'
    call putc
    mov dl, [drive]
    jmp 0x0000:0x7E00

disk_err:
    mov al, '!'
    call putc
    cli
hang:
    hlt
    jmp hang

putc:
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    ret

drive: db 0x80

times 510 - ($ - $$) db 0
dw 0xAA55
