; thunk16.asm — real-mode BIOS trampoline (ORG 0x7100)
; Copied to low memory at boot by entry.asm.

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
    mov byte [THUNK_REQ + 0x0A], 0x01
    jmp .return_pm

.reset:
    mov ah, 0x00
    mov dl, [THUNK_REQ + 0x00]
    int 0x13
    jmp .done

.set_video:
    mov al, [THUNK_REQ + 0x00]
    mov ah, 0x00
    int 0x10
    jmp .done_ok

.edd_probe:
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [THUNK_REQ + 0x00]
    int 0x13
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
    jmp .done

.chs_path:
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
    jmp .done

.edd_path:
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
    jmp .done

.edd_write:
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
