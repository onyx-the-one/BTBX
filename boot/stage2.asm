; stage2.asm - second-stage loader (loaded at 0x7E00)
BITS 16
ORG 0x7E00

KERNEL_LBA   equ 37
KERNEL_SECS  equ 128
SPT          equ 18
NHEADS       equ 2
DRIVE_STASH  equ 0x0600   ; safe scratch byte: BDA ends at 0x4FF, stage1 at 0x7C00

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov [drive], dl         ; save BIOS drive byte immediately

    mov al, 'S'
    call putc
    mov al, '2'
    call putc

    call a20_enable
    mov al, 'A'
    call putc

    call load_kernel
    mov al, 'K'
    call putc

    ; stash drive byte for kernel (read from our saved copy, not DL which lba_to_chs trashed)
    mov al, [drive]
    mov [DRIVE_STASH], al

    cli
    lgdt [gdtr]
    mov eax, cr0
    or  al, 1
    mov cr0, eax
    jmp 0x08:pmode32

BITS 32
pmode32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000
    jmp 0x10000

BITS 16

; lba_to_chs: AX=LBA -> CH=cyl, CL=sector(1-based), DH=head  TRASHES AX,BX,DX
lba_to_chs:
    xor dx, dx
    mov bx, SPT
    div bx              ; AX=track, DX=sector-0
    inc dx
    mov cl, dl          ; CL = sector (1-based)
    xor dx, dx
    mov bx, NHEADS
    div bx              ; AX=cylinder, DX=head
    mov ch, al
    mov dh, dl
    ret

load_kernel:
    mov word [buf_seg],  0x1000
    mov word [lba_cur],  KERNEL_LBA
    mov word [secs_rem], KERNEL_SECS
.chunk:
    mov ax, [lba_cur]
    call lba_to_chs         ; CH=cyl, CL=sec, DH=head

    mov al, SPT
    sub al, cl
    inc al                  ; AL = sectors left on track incl current
    xor ah, ah
    mov bx, [secs_rem]
    cmp bx, ax
    jbe .cnt_ok
    mov bx, ax
.cnt_ok:
    mov al, bl              ; AL = count for this read

    push ax                 ; save count
    mov bx, [buf_seg]
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    mov dl, [drive]         ; drive from saved copy
    int 0x13
    jc .load_fail
    pop ax                  ; AL = count read

    xor ah, ah
    mov bx, ax
    shl bx, 5               ; bx = count*32 (count*512/16)
    add word [buf_seg], bx
    mov bx, ax
    add word [lba_cur],  bx
    sub word [secs_rem], bx
    jnz .chunk
    ret

.load_fail:
    pop ax
    mov al, '!'
    call putc
    mov al, 'L'
    call putc
    mov al, ah
    shr al, 4
    add al, '0'
    cmp al, '9'
    jbe .n1
    add al, 7
.n1: call putc
    mov al, ah
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .n2
    add al, 7
.n2: call putc
    cli
.hang: hlt
    jmp .hang

kbc_wait_in:
    in al, 0x64
    test al, 0x02
    jnz kbc_wait_in
    ret

kbc_wait_out:
    in al, 0x64
    test al, 0x01
    jz kbc_wait_out
    ret

a20_enable:
    call kbc_wait_in
    mov al, 0xAD
    out 0x64, al
    call kbc_wait_in
    mov al, 0xD0
    out 0x64, al
    call kbc_wait_out
    in al, 0x60
    push ax
    call kbc_wait_in
    mov al, 0xD1
    out 0x64, al
    call kbc_wait_in
    pop ax
    or al, 0x02
    out 0x60, al
    call kbc_wait_in
    mov al, 0xAE
    out 0x64, al
    call kbc_wait_in
    ; Fast A20 via port 0x92 as backup
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

putc:
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    ret

drive:    db 0x80
buf_seg:  dw 0x1000
lba_cur:  dw 0
secs_rem: dw 0

gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; 0x08 32-bit code ring0
    dq 0x00CF92000000FFFF   ; 0x10 32-bit data ring0
    dq 0x00009A000000FFFF   ; 0x18 16-bit code (thunk)
    dq 0x000092000000FFFF   ; 0x20 16-bit data (thunk)

gdtr:
    dw gdtr - gdt - 1
    dd gdt

times 2048 - ($ - $$) db 0
