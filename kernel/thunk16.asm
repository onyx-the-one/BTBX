; thunk16.asm - real-mode BIOS trampoline
; Assembled as flat binary (thunk16.bin), copied to THUNK_BASE (0x7100) at runtime.
; Entered via far JMP with 16-bit code selector 0x18.

; Low-memory layout (all below 0x7100):
; 0x6E00 real-mode stack top (grows downward)
; 0x6E10-0x6E1F EDD packet scratch
; 0x6FE0 GDTR save (6 bytes) written by entry.asm sgdt
; 0x6FE6-0x6FEB (free)
; 0x6FEC ESP save (4 bytes) written by bios_disk_thunk wrapper
; 0x6FF0 return EIP (4 bytes) written by bios_disk_thunk wrapper

; Request block at 0x7000:
; [+0x00] uint8 drive
; [+0x01] uint8 head
; [+0x02] uint8 sector (1-based CHS)
; [+0x03] uint8 cylinder
; [+0x04] uint16 count
; [+0x06] uint32 buf_phys
; [+0x0A] uint8 status OUT (BIOS AH)
; [+0x0B] uint8 use_edd (0=CHS read, 1=EDD read, 2=CHS write)
; [+0x0C] uint32 lba_lo

BITS 16
ORG 0x7100

THUNK_REQ equ 0x7000
GDTR_SAVE equ 0x6FE0
ESP_SAVE  equ 0x6FEC
THUNK_RET equ 0x6FF0

%macro iowait 0
    out 0x80, al
%endmacro

thunk_entry:
    ; ── drop PE bit, flush pipeline into real mode ───────────────────────
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp 0x0000:.real

.real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6E00  ; real-mode stack, well below all save slots

    ; ── Restore BIOS PIC (0x08/0x70) & Unmask IRQs ──
    mov al, 0x11
    out 0x20, al
    iowait
    out 0xA0, al
    iowait
    mov al, 0x08      ; Master PIC vector offset (BIOS default)
    out 0x21, al
    iowait
    mov al, 0x70      ; Slave PIC vector offset (BIOS default)
    out 0xA1, al
    iowait
    mov al, 0x04
    out 0x21, al
    iowait
    mov al, 0x02
    out 0xA1, al
    iowait
    mov al, 0x01
    out 0x21, al
    iowait
    out 0xA1, al
    iowait

    xor al, al        ; 0x00 = unmask all IRQs
    out 0x21, al
    out 0xA1, al
    sti               ; Enable interrupts so the BIOS can handle IRQ 6

    cmp byte [THUNK_REQ + 0x0B], 0
    je .chs_path    ; 0 = CHS read
    cmp byte [THUNK_REQ + 0x0B], 1
    je .edd_path    ; 1 = EDD read
    ; 2 = CHS write (fall through)

; ── CHS write: INT 13h AH=03h ────────────────────────────────────────
.chs_write:
    ; compute ES:BX from buf_phys FIRST — this clobbers EAX/EBX
    mov ebx, [THUNK_REQ + 0x06]
    mov eax, ebx
    shr eax, 4
    and ebx, 0x0000000F
    mov es, ax
    mov bx, bx      ; BX already holds low nibble (no-op)

    ; load remaining registers AFTER ES:BX — EAX is now free for AH:AL
    mov ch, [THUNK_REQ + 0x03] ; cylinder
    mov cl, [THUNK_REQ + 0x02] ; sector (1-based)
    mov dh, [THUNK_REQ + 0x01] ; head
    mov dl, [THUNK_REQ + 0x00] ; drive
    mov al, [THUNK_REQ + 0x04] ; sector count — load AL last
    mov ah, 0x03               ; AH=03h write, AL=count
    int 0x13
    jmp .done

; ── CHS read: INT 13h AH=02h ─────────────────────────────────────────
.chs_path:
    ; IMPORTANT: compute ES:BX from buf_phys FIRST because mov eax,ebx
    ; clobbers AL. Load AL=count and AH=02 only after EAX is no longer needed.
    mov ebx, [THUNK_REQ + 0x06]
    mov eax, ebx
    shr eax, 4          ; EAX = buf_phys >> 4 (real-mode segment)
    and ebx, 0x0000000F ; EBX = buf_phys & 0xF (real-mode offset)
    mov es, ax
    mov bx, bx          ; BX already holds low nibble (no-op)

    ; load remaining registers — EAX is now free for AH:AL
    mov ch, [THUNK_REQ + 0x03] ; cylinder
    mov cl, [THUNK_REQ + 0x02] ; sector (1-based)
    mov dh, [THUNK_REQ + 0x01] ; head
    mov dl, [THUNK_REQ + 0x00] ; drive
    mov al, [THUNK_REQ + 0x04] ; sector count — load AL last, after EAX clobber
    mov ah, 0x02               ; AH=02h read, AL=count
    int 0x13
    jmp .done

; ── EDD read: INT 13h AH=42h ─────────────────────────────────────────
.edd_path:
    ; Build DAP at 0x6E10
    mov word [0x6E10], 0x0010   ; packet size=16, reserved=0
    mov ax, [THUNK_REQ + 0x04]
    mov [0x6E12], ax            ; sector count

    ; buf as seg:off from buf_phys
    mov eax, [THUNK_REQ + 0x06]
    mov ebx, eax
    shr eax, 4
    and ebx, 0x0000000F
    mov [0x6E14], bx            ; buffer offset
    mov [0x6E16], ax            ; buffer segment

    mov eax, [THUNK_REQ + 0x0C]
    mov [0x6E18], eax           ; LBA low 32
    mov dword [0x6E1C], 0       ; LBA high 32

    mov dl, [THUNK_REQ + 0x00]
    mov si, 0x6E10
    mov ah, 0x42
    int 0x13

.done:
    mov [THUNK_REQ + 0x0A], ah  ; save BIOS status byte (0 = success)

    cli                         ; Disable interrupts before switching back

    ; ── Re-mask & Restore PM PIC (0x20/0x28) ──
    mov al, 0x11
    out 0x20, al
    iowait
    out 0xA0, al
    iowait
    mov al, 0x20      ; Master PIC vector offset (kernel PM mapping)
    out 0x21, al
    iowait
    mov al, 0x28      ; Slave PIC vector offset (kernel PM mapping)
    out 0xA1, al
    iowait
    mov al, 0x04
    out 0x21, al
    iowait
    mov al, 0x02
    out 0xA1, al
    iowait
    mov al, 0x01
    out 0x21, al
    iowait
    out 0xA1, al
    iowait

    mov al, 0xFF      ; Mask all IRQs (no IDT yet)
    out 0x21, al
    out 0xA1, al

    ; ── return to 32-bit protected mode ──────────────────────────────────
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
    mov esp, [ESP_SAVE]         ; restore caller's 32-bit stack
    jmp [THUNK_RET]             ; jump back to thunk_returned in entry.asm
