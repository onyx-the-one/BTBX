; stage1.asm - 512-byte MBR boot sector
; Offset 0: jmp short start / nop (3 bytes)
; Offset 3: BPB placeholder (33 bytes) — mkfat.py patches these
; Offset 36+: real code

BITS 16
ORG 0x7C00

    jmp short start
    nop
    times 33 db 0       ; BPB placeholder (offsets 3-35)

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive], dl

    ; print "S1"
    mov al, 'S'
    call putc
    mov al, '1'
    call putc

    ; load stage2: 4 sectors starting at LBA 1 -> 0x07E0:0000
    mov ax, 0x07E0
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 4           ; sector count
    mov ch, 0           ; cylinder 0
    mov cl, 2           ; sector 2 (LBA 1)
    mov dh, 0           ; head 0
    mov dl, [drive]
    int 0x13
    jc disk_err

    mov al, '>'
    call putc

    mov dl, [drive]
    jmp 0x0000:0x7E00

disk_err:
    ; print "!E" + error code nibbles then freeze
    mov al, '!'
    call putc
    mov al, 'E'
    call putc
    ; AH = BIOS error code
    mov al, ah
    shr al, 4
    add al, '0'
    cmp al, '9'
    jbe .lo1
    add al, 7
.lo1:
    call putc
    mov al, ah
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .lo2
    add al, 7
.lo2:
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
