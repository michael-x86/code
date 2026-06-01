[org 0x7C00]

; Constants
KERNEL_SECTORS  equ 0x15A
KERNEL_LOAD_SEG equ 0x1000
KERNEL_LOAD_ADDR equ 0x00100000
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

    ; ---- Load Kernel from Disk (LBA via INT 13h AH=42h) ----
    ; Uses DAP (Disk Address Packet) for LBA access.
    ; Reads 1 sector at a time to keep the DAP simple.
    mov dword [dap_lba], 1          ; start at LBA 1 (sector 2 in CHS, after boot)
    mov word  [dap_count], 1
    mov word  [dap_off], 0
    mov word  [dap_seg], KERNEL_LOAD_SEG
    mov dword [sectors_left], KERNEL_SECTORS

.read_loop:
    cmp dword [sectors_left], 0
    je .read_done

    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    mov si, dap
    int 0x13
    jc disk_error

    ; Advance LBA
    inc dword [dap_lba]
    dec dword [sectors_left]

    ; Advance destination: add 512 to offset, handle segment overflow
    add word [dap_off], 512
    jnc .no_seg_oflow
    mov ax, [dap_seg]
    add ax, 0x1000
    mov [dap_seg], ax
.no_seg_oflow:
    jmp .read_loop

.read_done:
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
gdt_start:
gdt_null:   dq 0x0
gdt_code:   dq 0x00CF9A000000FFFF
gdt_data:   dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- DAP (must be in first 64K) ----
dap:
    db 0x10                 ; size of DAP (16 bytes)
    db 0                    ; reserved
    dw 1                    ; number of sectors to transfer
    dw 0                    ; offset of buffer
    dw KERNEL_LOAD_SEG      ; segment of buffer
    dd 1                    ; start LBA (low)
    dd 0                    ; start LBA (high)

; Aliased names for runtime modification
dap_count   equ dap + 2
dap_off     equ dap + 4
dap_seg     equ dap + 6
dap_lba     equ dap + 8

; ---- Data ----
msg_fail     db "DISK ERROR!", 0
BOOT_DRIVE   db 0
sectors_left dd 0

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

    mov esi, 0x00010000
    mov edi, 0x00100000
    mov ecx, KERNEL_SECTORS
    shl ecx, 9
    rep movsb

    jmp KERNEL_LOAD_ADDR

times 510 - ($-$$) db 0
dw 0xAA55
