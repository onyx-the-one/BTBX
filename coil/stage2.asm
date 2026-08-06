; stage2.asm — second-stage loader (0x7E00)
BITS 16
ORG 0x7E00

%include "coil/bootmeta.inc"

; DEBUG NOTE: SPT/NHEADS below are now only a LAST-RESORT fallback,
; used only if the ah=08h geometry query itself fails. They used to
; be the only geometry this loader ever used, which is exactly what
; caused a confirmed real-world failure: on a BIOS where ah=41h EDD
; probe/ah=42h EDD read are broken, falling back to CHS with these
; hardcoded floppy values (18/2) reads the WRONG sectors on
; USB/HDD-style media, silently loading garbage into the kernel
; buffer. See geometry-query code in load_kernel below.
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

; DEBUG: report exactly how the kernel was loaded and how many
; sectors were transferred, right before we commit to jumping into
; whatever we just loaded. If the jump then resets, you now know
; whether it read a plausible-looking sector count at least.
mov al, '('
call putc
cmp byte [use_edd], 1
je .dbg_used_edd
mov al, 'C'                ; C = CHS path was used
jmp .dbg_mode_done
.dbg_used_edd:
mov al, 'E'                ; E = EDD path was used
.dbg_mode_done:
call putc
mov al, ')'
call putc

mov al, [drive]
mov [DRIVE_STASH], al

; DEBUG: last checkpoint before the point of no return -- entering
; protected mode and jumping to the loaded kernel at 0x10000. If the
; machine resets right after this prints, the kernel image itself
; (or where it landed in memory) is bad, not the loader's own logic.
mov al, '['
call putc
mov al, 'J'
call putc
mov al, ']'
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
jmp 0x10000

BITS 16

; lba_to_chs: converts LBA in AX to CHS using the geometry values in
; [geom_spt]/[geom_heads], which are populated by get_geometry below
; (falling back to the SPT/NHEADS constants above only if the BIOS
; geometry query itself failed). This used to hardcode SPT/NHEADS
; directly, which was correct for floppy media only and silently
; broke on USB/HDD-emulation boot devices.

; DEBUG / HARDWARE NOTE: a zero SPT or HEADS value here causes a CPU
; divide-by-zero fault, which is unhandled in real mode and causes an
; immediate silent hard reset with no error message -- this is
; exactly what was observed on real hardware. get_geometry already
; guards against ever storing a zero value, but this is checked again
; here defensively: if either value is ever zero for any reason, we
; substitute the safe floppy defaults on the spot rather than letting
; the CPU fault. This can never mask a real error silently, since
; get_geometry always prints 'k' or 'x' -- this is purely a last-line
; crash guard.
lba_to_chs:
push ax
mov al, [geom_spt]
test al, al
jnz .spt_ok
mov byte [geom_spt], SPT
.spt_ok:
mov al, [geom_heads]
test al, al
jnz .heads_ok
mov byte [geom_heads], NHEADS
.heads_ok:
pop ax

xor dx, dx
movzx bx, byte [geom_spt]
div bx
inc dx
mov cl, dl
xor dx, dx
movzx bx, byte [geom_heads]
div bx
mov ch, al
mov dh, dl
ret

; get_geometry: queries the BIOS for real CHS geometry via
; int 13h ah=08h and stores the results in [geom_spt]/[geom_heads].
; On failure, leaves the SPT/NHEADS floppy defaults in place instead
; (already pre-loaded by the data declarations below) and prints a
; loud warning, since a silent wrong-geometry read is exactly the bug
; that caused this loader to load garbage and crash on real hardware.
get_geometry:
mov al, 'g'
call putc                  ; DEBUG: entering BIOS geometry query
mov dl, [drive]
mov ah, 0x08
xor cx, cx
xor dx, dx
push es
push di
xor di, di
mov es, di
int 0x13
pop di
pop es
jc .geom_fail

mov bl, cl
and bl, 0x3F
test bl, bl
jz .geom_fail               ; SPT of 0 is nonsensical, treat as failure

; DEBUG / HARDWARE NOTE: DH returns the BIOS's max head NUMBER
; (0-based), so head COUNT = DH+1. If DH comes back as 255 (a real,
; confirmed quirk on some USB/HDD-emulation devices), naively doing
; "inc al" wraps 255 -> 0, storing a head count of ZERO. lba_to_chs
; then does "div bx" with bx=0, which is a CPU divide-by-zero fault
; -- unhandled in real mode, causing an immediate silent hard reset
; with no error message at all. This is exactly the failure that was
; observed on real hardware (stopped dead right after the 'D'
; checkpoint, no '!' or 'x' or anything). Detect the wraparound
; explicitly and treat it as a failed query rather than ever storing
; a zero head count.
cmp dh, 0xFF
je .geom_fail                ; would wrap to 0 -- treat as failure instead
mov bh, dh
inc bh
test bh, bh
jz .geom_fail                ; belt-and-braces: never store a zero head count

; DEBUG / HARDWARE NOTE: confirmed on real hardware -- this BIOS
; reports SPT=18, HEADS=1 via ah=08h for the USB boot drive. HEADS=1
; is not a truthful description of this image's actual layout: the
; image was built by mkfat.py as a fixed 1.44MB floppy geometry
; (SPT=18, HEADS=2, 80 cylinders), and mkfat.py lays out every sector
; -- including the kernel's own boot metadata -- assuming that fixed
; 18/2 mapping. Trusting a reported HEADS=1 here causes lba_to_chs to
; compute a COMPLETELY different (wrong) CHS address for every LBA
; beyond the first cylinder, which produces garbage reads that
; eventually crash this flaky BIOS outright instead of just failing
; cleanly. Since this loader/image format is hardcoded to 18/2
; geometry everywhere else (stage1.asm's fixed CHS load, mkfat.py's
; layout), a reported HEADS=1 is treated as bogus/untrustworthy here
; and rejected, falling back to the correct 18/2 floppy geometry
; instead of blindly trusting whatever the BIOS claims.
cmp bh, 1
je .geom_fail                ; HEADS=1 is known-bad for this fixed floppy image layout

mov [geom_spt], bl
mov [geom_heads], bh
mov al, 'k'
call putc                   ; DEBUG: geometry query ok

; DEBUG: print the ACTUAL queried SPT/heads values in decimal so we
; can see exactly what the BIOS returned, not just that it "succeeded".
; Format: (SPT.HEADS) e.g. (63.16) -- this is the key missing piece
; needed to explain why the very first CHS read after this crashes
; the machine on real hardware when it worked fine via stage1.asm's
; hardcoded CHS values.
push ax
mov al, '('
call putc
pop ax
movzx ax, byte [geom_spt]
call put_dec8
mov al, '.'
call putc
movzx ax, byte [geom_heads]
call put_dec8
mov al, ')'
call putc

ret
.geom_fail:
mov al, 'x'
call putc                   ; DEBUG: geometry query failed, keeping floppy defaults
ret

load_kernel:
; DEBUG: always query real geometry first, regardless of which read
; path (EDD or CHS) ends up being used below -- the CHS fallback
; must never again silently use raw floppy geometry on non-floppy
; media, since that is the exact bug that caused a full boot loop.
call get_geometry

mov word [buf_seg], 0x1000
mov dword [lba_cur], KERNEL_LBA
mov word [secs_rem], KERNEL_SECS

; DEBUG / HARDWARE NOTE: EDD is permanently disabled below. On a real
; Core 2 Duo laptop, the EDD presence probe (int 13h ah=41h) reported
; "supported" (printed 'p' then 'k'), but the very next EDD extended
; read (int 13h ah=42h) then caused an immediate, silent hard reset
; of the whole machine -- not a hang, an actual reboot, with no
; further output at all. That means the ah=41h probe result CANNOT be
; trusted on this hardware: it lies about support, and acting on that
; lie by calling ah=42h is fatal. CHS (ah=02h) with real BIOS-reported
; geometry (see get_geometry above) is the only path proven safe on
; this machine, so EDD is skipped unconditionally here rather than
; probed for. Do not re-enable the probe/ah=42h path without a way to
; recover from a full silent BIOS reset, since it cannot be trapped
; or caught -- there is no carry flag to check if the BIOS itself
; reboots the machine.
mov byte [use_edd], 0
mov al, 'D'
call putc                  ; DEBUG: EDD skipped by design, going straight to CHS

; DEBUG / HARDWARE NOTE: on real hardware, boot got exactly as far as
; printing 'D' (EDD skipped) and then the WHOLE MACHINE silently hard
; resets before the first CHS read (ah=02h) can even report success
; or failure -- no '!', no hex error code, nothing. The only thing
; that changed right before this read is the new ah=08h geometry
; query added just above. Some real BIOSes (particularly on
; USB/HDD-emulation boot media) are known to leave the disk
; controller in a bad internal state after certain "unusual" INT 13h
; calls (ah=08h included) unless the disk system is explicitly reset
; via ah=00h before the next real transfer. This reset is cheap,
; universally supported since the original 5150 BIOS, and is the
; standard fix for exactly this class of BIOS quirk -- so it is
; issued here unconditionally, with its own checkpoint, so we can see
; whether the reset call itself is what's fatal, or if it's the read
; immediately after it.
mov al, 'r'
call putc                   ; DEBUG: entering post-geometry disk-system reset (ah=00h)
xor ax, ax
mov dl, [drive]
int 0x13
jnc .reset_ok
mov al, 'x'
call putc                   ; DEBUG: disk reset reported failure -- proceeding anyway, CHS read below will report its own status
jmp .reset_done
.reset_ok:
mov al, 'k'
call putc                   ; DEBUG: disk reset ok
.reset_done:

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
jc .edd_read_failed
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
.edd_read_failed:
; DEBUG: an EDD read (ah=42h) failed mid-transfer after the probe
; claimed support. Falling through to CHS from a partial state would
; double-load some sectors, so restart the whole transfer cleanly on
; the CHS path instead of trying to patch up where EDD left off.
mov al, 'x'
call putc
mov byte [use_edd], 0

.chs:
mov word [buf_seg], 0x1000
mov dword [lba_cur], KERNEL_LBA
mov word [secs_rem], KERNEL_SECS

; DEBUG: print the CHS triplet computed for the very FIRST chunk read,
; before ah=02h is ever issued. Previously the machine hard-reset
; silently right around here with no further output -- if '{...}'
; never appears, the crash is inside lba_to_chs itself; if it DOES
; appear, we finally see the exact (possibly invalid) CHS values
; being handed to the BIOS immediately before the reset.
mov ax, [lba_cur]
call lba_to_chs
mov al, '{'
call putc
mov al, 'c'
call putc
movzx ax, ch
call put_dec8
mov al, ','
call putc
mov al, 'h'
call putc
movzx ax, dh
call put_dec8
mov al, ','
call putc
mov al, 's'
call putc
movzx ax, cl
and al, 0x3F
call put_dec8
mov al, '}'
call putc

.chs_chunk:
mov ax, [lba_cur]
call lba_to_chs
mov al, byte [geom_spt]
sub al, cl
inc al
xor ah, ah
mov bx, [secs_rem]
cmp bx, ax
jbe .cnt_ok
mov bx, ax
.cnt_ok:
mov al, bl
push ax

; DEBUG: about to issue ah=02h -- if 'R' prints with nothing after it,
; THIS specific call is what resets the machine.
push ax
mov al, 'R'
call putc
pop ax

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
; DEBUG: a CHS read (ah=02h) failed outright -- this used to just
; print '!' and hang forever with no further detail. Now we also
; report AH (the BIOS error code) so a specific disk failure can be
; diagnosed instead of just seeing an unexplained freeze.
push ax
mov al, '!'
call putc
pop ax
mov al, ah
call put_hex8
cli
.hang:
hlt
jmp .hang

; DEBUG helper: prints AX (0-255) as up to 3 decimal digits via BIOS
; teletype, no leading zeros. Used to print exact BIOS-reported
; geometry values so failures can be diagnosed precisely instead of
; just knowing "it succeeded" or "it failed".
put_dec8:
push ax
push bx
push cx
push dx
xor cx, cx
mov bx, 10
.dec_divloop:
xor dx, dx
div bx
push dx
inc cx
test ax, ax
jnz .dec_divloop
.dec_printloop:
pop ax
add al, '0'
call putc
loop .dec_printloop
pop dx
pop cx
pop bx
pop ax
ret

; DEBUG helper: prints AL as two hex digits via BIOS teletype.
put_hex8:
push ax
push cx
mov cl, al
shr al, 4
call .nib
mov al, cl
and al, 0x0F
call .nib
pop cx
pop ax
ret
.nib:
cmp al, 10
jb .nib_digit
add al, 'A' - 10
jmp .nib_emit
.nib_digit:
add al, '0'
.nib_emit:
push ax
mov ah, 0x0E
xor bh, bh
int 0x10
pop ax
ret

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
geom_spt: db SPT            ; DEBUG: pre-seeded with floppy default, overwritten by get_geometry on success
geom_heads: db NHEADS       ; DEBUG: pre-seeded with floppy default, overwritten by get_geometry on success

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
