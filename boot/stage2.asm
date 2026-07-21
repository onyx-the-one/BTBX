; stage2.asm — second-stage loader (0x7E00)
BITS 16
ORG 0x7E00

%include "boot/bootmeta.inc"

SPT equ 18
NHEADS equ 2
DRIVE_STASH equ 0x0600

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

mov al, [drive]
mov [DRIVE_STASH], al

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
jmp 0x10000

BITS 16

lba_to_chs:
xor dx, dx
mov bx, SPT
div bx
inc dx
mov cl, dl
xor dx, dx
mov bx, NHEADS
div bx
mov ch, al
mov dh, dl
ret

; sectors left before [buf_seg]:0000 crosses a 64K linear boundary
; NOTE: clobbers ax, cx, dx — caller must save/restore cx if it holds
; CHS values that must survive this call (see .chs_chunk).
secs_to_64k:
movzx eax, word [buf_seg]
shl eax, 4
and eax, 0xFFFF
mov ecx, 0x10000
sub ecx, eax
shr ecx, 9
mov ax, cx
ret

load_kernel:
mov word [buf_seg], 0x1000
mov dword [lba_cur], KERNEL_LBA
mov word [secs_rem], KERNEL_SECS

mov dl, [drive]
cmp dl, 0x80
jb .chs

mov byte [use_edd], 0
mov ah, 0x41
mov bx, 0x55AA
int 0x13
jc .chs
cmp bx, 0xAA55
jne .chs
test cx, 1
jz .chs
mov byte [use_edd], 1

.edd_chunk:
cmp byte [use_edd], 1
jne .chs
mov ax, [secs_rem]
cmp ax, 127
jbe .edd_count_ok
mov ax, 127
.edd_count_ok:
mov [dap_count], ax
mov word [dap_off], 0
mov bx, [buf_seg]
mov [dap_seg], bx
mov eax, [lba_cur]
mov [dap_lba_lo], eax
mov dword [dap_lba_hi], 0
mov si, dap
mov ah, 0x42
mov dl, [drive]
int 0x13
jc .chs
mov ax, [dap_count]
mov bx, ax
shl bx, 5
add word [buf_seg], bx
xor dx, dx
add word [lba_cur], ax
adc word [lba_cur+2], dx
sub word [secs_rem], ax
jnz .edd_chunk
ret

.chs:
mov word [buf_seg], 0x1000
mov dword [lba_cur], KERNEL_LBA
mov word [secs_rem], KERNEL_SECS
.chs_chunk:
mov ax, [lba_cur]
call lba_to_chs
; CH/CL now hold cylinder/sector for this read — must survive until INT 13h
mov al, SPT
sub al, cl
inc al
xor ah, ah
mov bx, [secs_rem]
cmp bx, ax
jbe .cnt_track_ok
mov bx, ax
.cnt_track_ok:
push cx                 ; secs_to_64k clobbers cx — save the CHS values
call secs_to_64k
pop cx
cmp bx, ax
jbe .cnt_ok
mov bx, ax
.cnt_ok:
test bx, bx
jnz .cnt_nonzero
mov bx, 1
.cnt_nonzero:
mov al, bl
push ax
mov bx, [buf_seg]
mov es, bx
xor bx, bx
mov ah, 0x02
mov dl, [drive]
int 0x13
jc .load_fail
pop ax
xor ah, ah
mov bx, ax
shl bx, 5
add word [buf_seg], bx
mov bx, ax
add word [lba_cur], bx
sub word [secs_rem], bx
jnz .chs_chunk
ret

.load_fail:
mov al, '!'
call putc
cli
.hang:
hlt
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

drive: db 0x80
use_edd: db 0
buf_seg: dw 0x1000
lba_cur: dd 0
secs_rem: dw 0

dap: db 16, 0
dap_count: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba_lo: dd 0
dap_lba_hi: dd 0

gdt:
dq 0x0000000000000000
dq 0x00CF9A000000FFFF
dq 0x00CF92000000FFFF
dq 0x00009A000000FFFF
dq 0x000092000000FFFF

gdtr:
dw gdtr - gdt - 1
dd gdt

times 2048 - ($ - $$) db 0
