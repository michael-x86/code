[org 0x7C00]

KERNEL_SECTORS equ 132
BUFFER_SEG       equ 0x2000     ; Safe buffer zone (0x20000 linear), far above bootloader (0x7C00)
KERNEL_LOAD_ADDR equ 0x00100000 ; Final destination (1 MB)

CODE_SEG equ 0x08
DATA_SEG equ 0x10

start:
    cli
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov sp, 0x7C00              ; Stack grows downwards from 0x7C00 (perfectly safe)

    mov [BOOT_DRIVE], dl        ; Save boot drive

    ; ---- Enable A20 Line ----
    in al, 0x92
    or al, 00000010b
    out 0x92, al

    ; ---- Set up ES to point to our sequential destination buffer ----
    mov ax, BUFFER_SEG
    mov es, ax
    xor di, di                  ; ES:DI = 0x2000:0000

    mov cx, KERNEL_SECTORS      ; Loop tracking counter
.load_loop:
    push cx                     ; Save remaining loop count

    ; ---- DAP directly on the stack ----
    ;      QEMU ok, 16-byte aligned
    push dword 0                ; LBA upper 32-bits 
    push dword [LBA_START]      ; LBA lower 32-bits
    push es                     ; Dest Segment Pointer
    push di                     ; Dest Offset Pointer
    push word 1                 ; Counter 1 sector
    push word 0x0010            ; Size of packet (16 bytes)+reserved byte 0

    mov si, sp           

    mov ah,0x42                ; Extended Read 
    mov dl,[BOOT_DRIVE]        ; (Never thought I’d need that.)
    int 0x13
    jc disk_error

    add sp,16

    ; ---- Advance Dest Pointers ----
    add di,512                
    jnc .no_segment_carry      ; hit the 64 KB brick wall?
    mov ax,es
    add ax,0x1000              ; bump Segment up by 64KB
    mov es,ax
.no_segment_carry:

    inc dword [LBA_START]
    pop cx                     
    loop .load_loop

.load_done:

    ; ---- prepare for hyper jump to 32-bit Protected Mode ----
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:protected_mode

disk_error:
    mov ah,0x0B
    mov bh,0x00
    mov bl,0x04
    int 0x10
    jmp $

; ---- GDT ----
align 8
gdt_start:
gdt_null:
    dq 0x0
gdt_code: 
    dq 0x00CF9A000000FFFF
gdt_data: 
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end-gdt_start-1
    dd gdt_start

LBA_START  dd 1                 ; Start at Sector 2 (LBA index 1)
BOOT_DRIVE db 0

; ---- 32-BIT PROTECTED MODE ----
bits 32
protected_mode:
    mov ax,DATA_SEG
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax

    mov esp,0x90000            ; Move stack deep into high memory 

    ; ---- Relocate Data from Buffer to 1 MB ----
    mov esi,0x00020000         ; Source (BUFFER_SEG 0x2000:0000->0x20000 linear)
    mov edi,KERNEL_LOAD_ADDR   ; Destination (0x00100000)
    mov ecx,KERNEL_SECTORS
    shl ecx,7                  ; Sectors*512/4 =Total dwords to move
    rep movsd                 

    jmp KERNEL_LOAD_ADDR        ; Jump to kernel

; ---- PADDING ----
times 510 - ($-$$) db 0
dw 0xAA55
