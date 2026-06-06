; Minimal x86 "Hello World" kernel for emulator testing
; This writes "Hello" to VGA text mode at 0xB8000

[org 0x7C00]     ; Bootloader loads at 0x7C00
bits 16

start:
    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    ; Enable A20 (simplified - just port 0x92)
    in al, 0x92
    or al, 2
    out 0x92, al
    
    ; Load kernel to 0x100000 (1MB)
    ; (In real emulator, we'd use INT 13h or ATA)
    ; For now, just jump to protected mode setup
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; Enter protected mode
    cli
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to 32-bit code
    jmp 0x08:protected_mode

; ============================================================
; 32-bit protected mode
; ============================================================
bits 32
protected_mode:
    ; Set up segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    
    ; Clear screen (write spaces to VGA)
    mov edi, 0xB8000
    mov ecx, 80 * 25
    mov eax, 0x0720  ; Attribute 0x07 (white on black), char 0x20 (space)
    rep stosw
    
    ; Write "Hello" to VGA at (row=0, col=0)
    mov edi, 0xB8000  ; VGA text buffer
    mov byte [edi], 'H'
    mov byte [edi + 1], 0x0F  ; White on black
    add edi, 2
    mov byte [edi], 'e'
    mov byte [edi + 1], 0x0F
    add edi, 2
    mov byte [edi], 'l'
    mov byte [edi + 1], 0x0F
    add edi, 2
    mov byte [edi], 'l'
    mov byte [edi + 1], 0x0F
    add edi, 2
    mov byte [edi], 'o'
    mov byte [edi + 1], 0x0F
    
    ; Halt
.halt:
    hlt
    jmp .halt

; ============================================================
; GDT (Global Descriptor Table)
; ============================================================
align 8
gdt_start:
gdt_null:   dq 0x0                    ; Null descriptor
gdt_code:   dq 0x00CF9A000000FFFF  ; Code segment (0x08)
gdt_data:   dq 0x00CF92000000FFFF  ; Data segment (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($-$$) db 0
dw 0xAA55
