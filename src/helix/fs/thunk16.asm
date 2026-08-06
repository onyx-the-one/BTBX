; thunk16.asm — real-mode BIOS trampoline (ORG 0x7100)
; Copied to low memory at boot by entry.asm.
;
; DEBUG: every opcode branch below prints a checkpoint character via
; BIOS teletype (int 10h ah=0Eh) BEFORE and AFTER its int 13h/int 10h
; call. This is deliberate and permanent -- this code runs in real
; mode where kernel.c's VGA driver has no visibility, so BIOS
; teletype is the only way to see what happened if something hangs
; here. Do not remove these. Every printed pair is [opcode-letter]
; then either 'k' (call returned, carry clear) or 'x' (call returned,
; carry set / error) -- if you see the opening letter with no closing
; k/x, that specific BIOS call itself is what hung.

BITS 16
ORG 0x7100

THUNK_REQ equ 0x7000
GDTR_SAVE equ 0x6FE0
ESP_SAVE  equ 0x6FEC
THUNK_RET equ 0x6FF0

thunk_entry:
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp 0x0000:.real

.real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6E00
    sti         ; CRITICAL: Enable interrupts so floppy IRQ6 works!

    ; DEBUG: confirmed we are alive in real mode with a working stack
    mov al, 'R'
    call dbg_putc

    cmp byte [THUNK_REQ + 0x0B], 0
    je .chs_path
    cmp byte [THUNK_REQ + 0x0B], 1
    je .edd_path
    cmp byte [THUNK_REQ + 0x0B], 2
    je .chs_write
    cmp byte [THUNK_REQ + 0x0B], 3
    je .edd_write
    cmp byte [THUNK_REQ + 0x0B], 4
    je .edd_probe
    cmp byte [THUNK_REQ + 0x0B], 5
    je .reset
    cmp byte [THUNK_REQ + 0x0B], 6
    je .set_video
    cmp byte [THUNK_REQ + 0x0B], 7
    je .get_geometry

    ; DEBUG: opcode byte didn't match any known operation -- this is
    ; a real bug (caller passed garbage), not a BIOS quirk. Make it
    ; loud and distinct so it's never confused with a BIOS hang.
    mov al, '?'
    call dbg_putc
    mov byte [THUNK_REQ + 0x0A], 0x01
    jmp .return_pm

.reset:
    mov al, 'r'
    call dbg_putc               ; DEBUG: entering disk-reset (ah=00h)
    mov ah, 0x00
    mov dl, [THUNK_REQ + 0x00]
    int 0x13
    call dbg_result             ; DEBUG: prints 'k' or 'x' based on carry
    jmp .done

.set_video:
    mov al, 'V'
    call dbg_putc               ; DEBUG: entering set-video-mode (int 10h)
    mov al, [THUNK_REQ + 0x00]
    mov ah, 0x00
    int 0x10
    mov al, 'k'
    call dbg_putc               ; DEBUG: int 10h ah=00h always "returns" ok
    jmp .done_ok

; ── AH=08h — Get Drive Geometry ─────────────────────────────────────
; Some BIOSes (confirmed on a real Core 2 Duo laptop) hang on both the
; EDD presence-check (AH=41h) AND the EDD extended read (AH=42h), even
; though plain legacy CHS (AH=02h/03h) works fine. This call fetches
; the BIOS's own idea of sectors-per-track / head count so the CHS
; path can compute correct addresses instead of assuming floppy
; geometry (18 SPT / 2 heads), which is wrong for USB/HDD-style media.
; Returns: byte 0x08 = sectors-per-track, byte 0x09 = head count.
.get_geometry:
    mov al, 'g'
    call dbg_putc               ; DEBUG: entering geometry query (ah=08h)
    mov dl, [THUNK_REQ + 0x00]
    mov ah, 0x08
    xor cx, cx
    xor dx, dx
    int 0x13
    call dbg_result              ; DEBUG: prints 'k' or 'x' based on carry
    jc .geom_fail
    mov al, cl
    and al, 0x3F                 ; bits 0-5 of CL = sectors per track
    mov [THUNK_REQ + 0x08], al
    mov al, dh
    inc al                       ; DH = max head number (0-based) -> head count
    mov [THUNK_REQ + 0x09], al
    mov ah, 0x00
    jmp .done_ok
.geom_fail:
    mov ah, 0x01
    jmp .done_err

.edd_probe:
    mov al, 'p'
    call dbg_putc               ; DEBUG: entering EDD probe (ah=41h) -- KNOWN
                                 ; TO HANG on at least one real BIOS. If 'p'
                                 ; prints with no follow-up 'k'/'x', this is it.
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [THUNK_REQ + 0x00]
    int 0x13
    call dbg_result
    jc .probe_fail
    cmp bx, 0xAA55
    jne .probe_fail
    test cx, 1
    jz .probe_fail
    mov ah, 0x00
    jmp .done_ok
.probe_fail:
    mov ah, 0x01
    jmp .done_err

.chs_write:
    mov al, 'W'
    call dbg_putc               ; DEBUG: entering CHS write (ah=03h)
    mov ebx, [THUNK_REQ + 0x06]
    mov eax, ebx
    shr eax, 4
    and ebx, 0x0000000F
    mov es, ax
    mov bx, bx
    mov ch, [THUNK_REQ + 0x03]
    mov cl, [THUNK_REQ + 0x02]
    mov dh, [THUNK_REQ + 0x01]
    mov dl, [THUNK_REQ + 0x00]
    mov al, [THUNK_REQ + 0x04]
    mov ah, 0x03
    int 0x13
    call dbg_result
    jmp .done

.chs_path:
    mov al, 'C'
    call dbg_putc               ; DEBUG: entering CHS read (ah=02h)
    mov ebx, [THUNK_REQ + 0x06]
    mov eax, ebx
    shr eax, 4
    and ebx, 0x0000000F
    mov es, ax
    mov bx, bx
    mov ch, [THUNK_REQ + 0x03]
    mov cl, [THUNK_REQ + 0x02]
    mov dh, [THUNK_REQ + 0x01]
    mov dl, [THUNK_REQ + 0x00]
    mov al, [THUNK_REQ + 0x04]
    mov ah, 0x02
    int 0x13
    call dbg_result
    jmp .done

.edd_path:
    mov al, 'e'
    call dbg_putc               ; DEBUG: entering EDD read (ah=42h) -- ALSO
                                 ; CONFIRMED TO HANG on the same BIOS as the
                                 ; probe above. Not used by fat12.c anymore
                                 ; (EDD is disabled there) but kept + traced
                                 ; here in case something else invokes it.
    mov word [0x6E10], 0x0010
    mov ax, [THUNK_REQ + 0x04]
    mov [0x6E12], ax
    mov eax, [THUNK_REQ + 0x06]
    mov ebx, eax
    shr eax, 4
    and ebx, 0x0000000F
    mov [0x6E14], bx
    mov [0x6E16], ax
    mov eax, [THUNK_REQ + 0x0C]
    mov [0x6E18], eax
    mov dword [0x6E1C], 0
    mov dl, [THUNK_REQ + 0x00]
    mov si, 0x6E10
    mov ah, 0x42
    int 0x13
    call dbg_result
    jmp .done

.edd_write:
    mov al, 'E'
    call dbg_putc               ; DEBUG: entering EDD write (ah=43h)
    mov word [0x6E10], 0x0010
    mov ax, [THUNK_REQ + 0x04]
    mov [0x6E12], ax
    mov eax, [THUNK_REQ + 0x06]
    mov ebx, eax
    shr eax, 4
    and ebx, 0x0000000F
    mov [0x6E14], bx
    mov [0x6E16], ax
    mov eax, [THUNK_REQ + 0x0C]
    mov [0x6E18], eax
    mov dword [0x6E1C], 0
    mov dl, [THUNK_REQ + 0x00]
    mov si, 0x6E10
    mov ah, 0x43
    int 0x13
    call dbg_result

.done:
    jc .done_err
.done_ok:
    mov ah, 0x00
.done_err:
    cmp ah, 0
    jne .store_err
    jc .store_cf
    jmp .store_err
.store_cf:
    mov ah, 0x01
.store_err:
    mov [THUNK_REQ + 0x0A], ah

    ; DEBUG: final status byte about to be handed back to protected
    ; mode -- '0' means success, 'F' means the operation failed. This
    ; is the LAST thing printed before we attempt the PM transition,
    ; so if you see this but the kernel still appears frozen, the bug
    ; is in the return-to-protected-mode sequence below, not the BIOS
    ; call itself.
    cmp byte [THUNK_REQ + 0x0A], 0
    jne .dbg_final_fail
    mov al, '0'
    jmp .dbg_final_emit
.dbg_final_fail:
    mov al, 'F'
.dbg_final_emit:
    call dbg_putc

.return_pm:
    cli         ; CRITICAL: Disable interrupts before returning to PM!
    lgdt [GDTR_SAVE]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:.prot32

BITS 32
.prot32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, [ESP_SAVE]
    jmp [THUNK_RET]

; ── DEBUG helpers: BIOS teletype output (real mode only) ────────────
; Placed at the very end, AFTER .prot32, so NASM's dot-label scoping
; never re-parents .prot32 under one of these helper labels (that
; exact mistake broke a previous build -- keep these helpers last).
BITS 16

; dbg_putc: prints AL via int 10h ah=0Eh. Clobbers ah, bh, bl only;
; preserves everything else since it's called from mid-sequence code
; that still needs its registers (cx/dx especially, for CHS/EDD).
dbg_putc:
    push ax
    push bx
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    ret

; dbg_result: call IMMEDIATELY after an int 13h, before anything else
; touches the flags register. Prints 'k' if carry is clear (BIOS
; reported success) or 'x' if carry is set (BIOS reported an error).
; Preserves all registers and, critically, the carry flag itself, so
; the caller's own jc/jnc logic right after still works correctly.
dbg_result:
    pushf
    push ax
    jc .dbg_result_fail
    mov al, 'k'
    jmp .dbg_result_emit
.dbg_result_fail:
    mov al, 'x'
.dbg_result_emit:
    call dbg_putc
    pop ax
    popf
    ret
