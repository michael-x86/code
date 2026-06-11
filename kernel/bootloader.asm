[org 0x7C00]
bits 16

; ── Constants ────────────────────────────────────────────────────────────────
KERNEL_SECTORS  equ 0xA8
; Max sectors per INT 13h call.  SeaBIOS 1.17 refuses reads larger than 127
; sectors; 64 is safe on every BIOS and keeps the segment math simple.
MAX_SEC         equ 64

KERNEL_LOAD_SEG equ 0x1000
KERNEL_LOAD_OFF equ 0x0000
KERNEL_LOAD_ADDR equ 0x10000       ; = KERNEL_LOAD_SEG * 16 + KERNEL_LOAD_OFF

CODE_SEG        equ 0x08
DATA_SEG        equ 0x10


; ── Entry ────────────────────────────────────────────────────────────────────
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

    ; ── Load kernel via LBA (chunked loop) ──────────────────────────────────
    ; Check EDD (INT 13h Extensions) support
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .chs_load
    cmp bx, 0xAA55
    jne .chs_load

    ; LBA chunked load
    mov si, dap
    mov word [dap + 8], 1               ; start LBA low word  = 1
    mov word [dap + 10], 0              ; start LBA high word = 0
    mov word [dap + 4], KERNEL_LOAD_OFF ; buffer offset
    mov word [dap + 6], KERNEL_LOAD_SEG ; buffer segment
    mov cx, KERNEL_SECTORS              ; total sectors remaining
.next_chunk:
    mov ax, MAX_SEC
    cmp cx, ax
    jae .have_count
    mov ax, cx                          ; last chunk may be smaller
.have_count:
    mov word [dap + 2], ax              ; sectors this call
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    sub cx, ax                          ; remaining after this chunk
    jz load_done                        ; exactly zero → all done

    ; Advance the DAP buffer pointer.  Each chunk is sectors × 512 bytes.
    push ax
    mov dx, MAX_SEC
    shl dx, 9                           ; × 512 = 0x8000 = 32768
    add word [dap + 4], dx              ; offset += 32768 bytes
    jnc .no_seg_ov
    add word [dap + 6], 0x1000          ; carry → bump segment by 4096
.no_seg_ov:
    pop ax
    add word [dap + 8], ax              ; LBA low  += sectors this call
    adc word [dap + 10], 0              ; LBA high += carry
    jmp .next_chunk

    ; ── CHS fallback (small kernels only) ───────────────────────────────────
.chs_load:
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx                     ; ES:BX = 0x1000:0x0000
    mov cx, KERNEL_SECTORS         ; total sectors remaining
    mov dx, [BOOT_DRIVE]
    mov dh, 0                      ; head 0
    mov al, 2                      ; start sector = 2 (LBA 1)
.next_chs:
    mov ah, MAX_SEC
    cmp cx, ax
    jae .chs_count
    mov ax, cx
.chs_count:
    push cx
    mov ah, 0x02
    ; CHS: cylinder 0 in ch[7:0]+cl[7:6], sector in cl[5:0], head in dh
    ; We start at sector 2 (LBA 1) and advance linearly.
    int 0x13
    jc disk_error
    pop cx
    sub cx, ax
    jz load_done
    ; Advance to next head/cylinder
    add bx, MAX_SEC * 512
    jnc .next_chs
    ; This would need proper CHS geometry tracking; for kernels >64 sectors
    ; EDD must be available.  But we try anyway.
    add ax, 63
    cmp al, 63
    jbe .next_chs
    inc dh                       ; advance head
    mov al, 1                    ; restart at sector 1 of new head
    jmp .next_chs


; ── Post-load ────────────────────────────────────────────────────────────────
load_done:
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


; ── GDT ──────────────────────────────────────────────────────────────────────
align 8
gdt_start:
gdt_null:   dq 0x0
gdt_code:   dq 0x00CF9A000000FFFF
gdt_data:   dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start


; ── Data ─────────────────────────────────────────────────────────────────────
msg_fail     db "DISK ERROR!", 0
BOOT_DRIVE   db 0

; Disk Address Packet (for INT 13h AH=42h LBA read, fields updated in-flight)
align 4
dap:
    db 0x10                 ; size of packet (16 bytes)
    db 0                    ; reserved
    dw MAX_SEC              ; sectors this call (overwritten by loader loop)
    dw KERNEL_LOAD_OFF      ; offset of buffer  (overwritten by loader loop)
    dw KERNEL_LOAD_SEG      ; segment of buffer (overwritten on overflow)
    dd 1                    ; start LBA          (overwritten by loader loop)
    dd 0                    ; upper 32 bits of LBA


; ── 32-bit Protected Mode ────────────────────────────────────────────────────
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
    mov edi, 0x00100000           ; dest = 1 MB
    mov ecx, KERNEL_SECTORS
    shl ecx, 9                   ; sectors × 512 = bytes
    cld
    rep movsb

    ; Store FS disk info at physical 0x500 for the kernel to read:
    ;   [0x500]  dword  ATA port base  = 0x01F0  (primary IDE controller)
    ;   [0x504]  byte   drive select   = 0xF0   (slave — QEMU index=1)
    ;   [0x508]  dword  FS base LBA    = 0      (FS starts at LBA 0 of this disk)
    mov dword [0x500], 0x01F0
    mov byte  [0x504], 0xF0
    mov dword [0x508], 0

    ; Transfer control to the kernel physical entry at 1 MB.
    mov eax, 0x00100000
    jmp eax

times 510 - ($-$$) db 0
dw 0xAA55
