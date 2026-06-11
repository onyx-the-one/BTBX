; entry.asm - 32-bit kernel entry point, linked at 0x10000
BITS 32

GLOBAL _start
GLOBAL bios_disk_thunk
EXTERN kernel_main

THUNK_BASE  equ 0x7100
THUNK_REQ   equ 0x7000
GDTR_SAVE   equ 0x6FE0   ; 6 bytes
ESP_SAVE    equ 0x6FEC   ; 4 bytes
THUNK_RET   equ 0x6FF0   ; 4 bytes
DRIVE_STASH equ 0x0600   ; drive byte written by stage2 before entering PM

SECTION .text
_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    ; zero BSS
    extern _bss_start
    extern _bss_end
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; copy thunk16 blob to low memory
    mov esi, thunk16_blob
    mov edi, THUNK_BASE
    mov ecx, thunk16_blob_end - thunk16_blob
    rep movsb

    sgdt [GDTR_SAVE]

    ; read drive byte stashed by stage2 at DRIVE_STASH and pass to kernel_main
    movzx eax, byte [DRIVE_STASH]
    push eax                ; arg: boot_drive
    call kernel_main
    add esp, 4

    cli
.hang:
    hlt
    jmp .hang

; bios_disk_thunk(drive, head, sector, cyl, count, buf_phys, use_edd, lba_lo)
bios_disk_thunk:
    pushad
    mov [ESP_SAVE], esp

    mov eax, esp
    add eax, 36             ; past pushad (32 bytes) + saved ESP_SAVE overhead = 36

    mov bl, [eax+0]
    mov [THUNK_REQ + 0x00], bl   ; drive
    mov bl, [eax+4]
    mov [THUNK_REQ + 0x01], bl   ; head
    mov bl, [eax+8]
    mov [THUNK_REQ + 0x02], bl   ; sector
    mov bl, [eax+12]
    mov [THUNK_REQ + 0x03], bl   ; cylinder
    mov bx, [eax+16]
    mov [THUNK_REQ + 0x04], bx   ; count
    mov ebx, [eax+20]
    mov [THUNK_REQ + 0x06], ebx  ; buf_phys
    mov bl, [eax+24]
    mov [THUNK_REQ + 0x0B], bl   ; use_edd
    mov ebx, [eax+28]
    mov [THUNK_REQ + 0x0C], ebx  ; lba_lo

    mov dword [THUNK_RET], thunk_returned
    jmp 0x18:THUNK_BASE

thunk_returned:
    movzx eax, byte [THUNK_REQ + 0x0A]  ; CF result
    mov [esp+28], eax                    ; overwrite EAX slot in pushad frame

    popad
    ret

SECTION .data
thunk16_blob:
    incbin "kernel/thunk16.bin"
thunk16_blob_end:
