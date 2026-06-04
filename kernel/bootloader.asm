[org 0x7C00]
bits 16

; Constants
KERNEL_SECTORS  equ 0x27
KERNEL_LOAD_SEG equ 0x1000
KERNEL_LOAD_OFF equ 0x0000
KERNEL_LOAD_ADDR equ 0x10000       ; = KERNEL_LOAD_SEG * 16 + KERNEL_LOAD_OFF

CODE_SEG        equ 0x08
DATA_SEG        equ 0x10

start:
    cli
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov sp, 0x9000

    mov [BOOT_DRIVE], dl

    ; Enable A20
    in al, 0x92
    or al, 00000010b
    out 0x92, al

    ; ---- Load Kernel from Disk ----
    ; Try LBA (INT 13h AH=42h) first, fall back to CHS (AH=02h) on failure.

    ; Check EDD support
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .chs_fallback
    cmp bx, 0xAA55
    jne .chs_fallback

    ; LBA read: read KERNEL_SECTORS from LBA 1 into KERNEL_LOAD_SEG:KERNEL_LOAD_OFF
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    jmp load_done

.chs_fallback:
    ; CHS read: KERNEL_SECTORS from Cyl 0 Head 1 Sector 2
    ; (Cyl 0, Head 0, Sector 1 is the bootloader itself)
    ; CHS(0, 1, 2) = LBA 63... wait, no.
    ; CHS addressing: LBA 0 = (0,0,1), LBA 1 = (0,0,2), ..., LBA 62 = (0,0,63)
    ; LBA 63 = (0,1,1), etc.
    ; For a small kernel (< 62 sectors), all sectors are in cyl 0, head 0.
    ; LBA 1 = (0,0,2)
    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0            ; cylinder 0
    mov cl, 2            ; start at sector 2 (LBA 1)
    mov dh, 0            ; head 0
    mov dl, [BOOT_DRIVE]
    xor bx, bx           ; offset = 0
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    int 0x13
    jc disk_error

load_done:

    ; ---- Transition to Protected Mode ----
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:protected_mode

disk_error:
    mov si, msg_fail
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
.ps_next:
    lodsb
    test al, al
    jz .ps_done
    int 0x10
    jmp .ps_next
.ps_done:
    ret

; ---- GDT ----
align 8
gdt_start:
gdt_null:   dq 0x0
gdt_code:   dq 0x00CF9A000000FFFF
gdt_data:   dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- Data ----
msg_fail     db "DISK ERROR!", 0
BOOT_DRIVE   db 0

; Disk Address Packet (for INT 13h AH=42h LBA read)
align 4
dap:
    db 0x10                 ; size of packet (16 bytes)
    db 0                    ; reserved
    dw KERNEL_SECTORS       ; number of sectors to read
    dw KERNEL_LOAD_OFF      ; offset of buffer
    dw KERNEL_LOAD_SEG      ; segment of buffer
    dd 1                    ; start LBA (sector 1, after bootloader)
    dd 0                    ; upper 32 bits of LBA (unused)

; ---- 32-bit Protected Mode ----
bits 32
protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000

    ; Copy kernel from 0x10000 to 0x100000
    mov esi, KERNEL_LOAD_ADDR     ; source = 0x10000
    mov edi, 0x00100000           ; dest = 1MB
    mov ecx, KERNEL_SECTORS
    shl ecx, 9                   ; sectors * 512 = bytes
    cld
    rep movsb

    ; Store FS disk info at physical 0x500 for the kernel to read:
    ;   [0x500]  dword  ATA port base  = 0x01F0  (primary IDE controller)
    ;   [0x504]  byte   drive select   = 0xF0   (slave — QEMU index=1)
    ;   [0x508]  dword  FS base LBA    = 0      (FS starts at LBA 0 of this disk)
    mov dword [0x500], 0x01F0
    mov byte  [0x504], 0xF0
    mov dword [0x508], 0

    ; Transfer control to the kernel physical entry at 1MB.
    ; CS is already the protected-mode code segment (0x08), so a near jump is sufficient.
    mov eax, 0x00100000
    jmp eax

times 510 - ($-$$) db 0
dw 0xAA55
