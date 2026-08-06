; stage1.asm — 512-byte MBR/VBR boot sector
; Offsets 0-2 : jmp short start / nop
; Offsets 3-35: BPB placeholder (patched by mkfat.py)
; Offset  36+ : real code

BITS 16
ORG 0x7C00

    jmp short start
    nop
    times 33 db 0           ; BPB placeholder

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [drive], dl

    mov al, 'S' & 0x7F
    call putc
    mov al, '1'
    call putc

    ; load stage2: 4 sectors at LBA 1 → 0x07E0:0000
    mov ax, 0x07E0
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 4
    mov ch, 0
    mov cl, 2       ; sector 2 = LBA 1
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
.hang:
    hlt
    jmp .hang

putc:
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    ret

drive: db 0x80

    times 510 - ($ - $$) db 0
    dw 0xAA55
