; stage2.asm - second-stage loader (loaded at 0x7E00)
; Prints checkpoints, enables A20, loads kernel, enters 32-bit PM.
; Must assemble to exactly 2048 bytes (4 sectors).

BITS 16
ORG 0x7E00

start:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov [drive], dl

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

    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
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
    mov eax, 0x10000
    jmp eax

BITS 16

; load_kernel
; Reads kernel from sectors 5+ into 0x1000:0000.
; Track chunks for standard 1.44MB geometry.
load_kernel:
    mov word [buf_seg], 0x1000

    mov cx, 0x0006
    mov dh, 0
    mov al, 13
    call read_chunk
    jc load_fail

    mov cx, 0x0001
    mov dh, 1
    mov al, 18
    call read_chunk
    jc load_fail

    mov cx, 0x0101
    mov dh, 0
    mov al, 18
    call read_chunk
    jc load_fail

    mov cx, 0x0101
    mov dh, 1
    mov al, 18
    call read_chunk
    jc load_fail

    ret

load_fail:
    mov al, '!'
    call putc
    cli
load_hang:
    hlt
    jmp load_hang

; read_chunk
; CH = cylinder, CL = starting sector, DH = head, AL = count
; Reads to [buf_seg]:0000 and advances buf_seg by AL*32 paragraphs.
read_chunk:
    push ax
    mov bx, [buf_seg]
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    mov dl, [drive]
    int 0x13
    jc read_err

    pop ax
    xor ah, ah
    mov bl, 32
    mul bl
    add word [buf_seg], ax
    clc
    ret

read_err:
    pop ax
    stc
    ret

; a20_enable: keyboard controller method, then fast gate
; wait until input buffer empty
kbc_wait_in:
    in al, 0x64
    test al, 0x02
    jnz kbc_wait_in
    ret

; wait until output buffer full
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

drive:   db 0x80
buf_seg: dw 0x1000

gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF

gdtr:
    dw gdtr - gdt - 1
    dd gdt

times 2048 - ($ - $$) db 0
