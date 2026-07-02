; entry.asm — 32-bit kernel entry point, linked at 0x10000
BITS 32

GLOBAL _start
GLOBAL bios_disk_thunk
EXTERN kernel_main

THUNK_BASE  equ 0x7100
THUNK_REQ   equ 0x7000
GDTR_SAVE   equ 0x6FE0
ESP_SAVE    equ 0x6FEC
THUNK_RET   equ 0x6FF0
DRIVE_STASH equ 0x0600

SECTION .text
_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    extern _bss_start
    extern _bss_end
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; copy thunk16 blob to 0x7100
    mov esi, thunk16_blob
    mov edi, THUNK_BASE
    mov ecx, thunk16_blob_end - thunk16_blob
    rep movsb

    sgdt [GDTR_SAVE]

    movzx eax, byte [DRIVE_STASH]
    push eax
    call kernel_main
    add esp, 4

    cli
.hang:
    hlt
    jmp .hang

; uint8_t bios_disk_thunk(drive, head, sector, cyl, count, buf_phys, use_edd, lba_lo)
; Calling convention: cdecl — args on stack above saved regs (pushad = 32 bytes)
bios_disk_thunk:
    pushad
    mov [ESP_SAVE], esp

    mov eax, esp
    add eax, 36         ; skip pushad(32) + return addr(4)

    mov bl, [eax+0]
    mov [THUNK_REQ + 0x00], bl  ; drive
    mov bl, [eax+4]
    mov [THUNK_REQ + 0x01], bl  ; head
    mov bl, [eax+8]
    mov [THUNK_REQ + 0x02], bl  ; sector
    mov bl, [eax+12]
    mov [THUNK_REQ + 0x03], bl  ; cyl
    mov bx, [eax+16]
    mov [THUNK_REQ + 0x04], bx  ; count
    mov ebx, [eax+20]
    mov [THUNK_REQ + 0x06], ebx ; buf_phys
    mov bl, [eax+24]
    mov [THUNK_REQ + 0x0B], bl  ; use_edd
    mov ebx, [eax+28]
    mov [THUNK_REQ + 0x0C], ebx ; lba_lo

    mov dword [THUNK_RET], thunk_returned
    jmp 0x18:THUNK_BASE

thunk_returned:
    movzx eax, byte [THUNK_REQ + 0x0A]
    mov [esp+28], eax   ; poke return value into pushad's eax slot

    popad
    ret

SECTION .data
thunk16_blob:
    incbin "src/kernel/fs/thunk16.bin"
thunk16_blob_end:
