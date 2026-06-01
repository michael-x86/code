[org 0x7C00]

; I would be happy if you allowed my contact details to remain.
; Best regards,
; michael@nordstedt.eu

; Constants for memory management and segments
KERNEL_SECTORS  equ 0x6E
KERNEL_LOAD_SEG   equ 0x1000     
KERNEL_LOAD_ADDR  equ 0x00100000  ; 1 MB (safe, below 4 MB)
CODE_SEG          equ 0x08        ; Offset - code segment in GDT
DATA_SEG          equ 0x10        ; Offset - data segment in GDT

start:
    cli                         ; Disable interrupts
    xor ax,ax                  ; Initialize segments to 0
    mov ss,ax
    mov ds,ax
    mov es,ax
    mov sp,0x9000              ; safe real-mode stack

    mov [BOOT_DRIVE],dl       

    ; ---- Enable A20 Line ----
    ; Required to access memory above 1MB
    in al,0x92
    or al,00000010b
    out 0x92,al
    
    ; ---- Load Kernel from Disk ----
    mov ax,KERNEL_LOAD_SEG     ; Destination segment
    mov es,ax
    xor bx,bx                  ; Destination offset (0)

    mov ah,0x02                ; BIOS Read Sectors function
    mov al,KERNEL_SECTORS      ; Number of sectors to read
    mov ch,0x00                ; Cylinder 0
    mov cl,0x02                ; Start at Sector 2 (1 is bootloader)
    mov dh,0x00                ; Head 0
    mov dl,[BOOT_DRIVE]        ; Read from boot drive
    int 0x13                    
    jc disk_error               
    
    ; ---- Transition to Protected Mode ----
    cli                       
    lgdt [gdt_descriptor]       ; Load Global Descriptor Table

    mov eax,cr0
    or eax,1                   ; Set PE (Protection Enable) bit
    mov cr0,eax

    ; Far jump to flush the CPU pipeline & switch to 32-bit segment
    jmp CODE_SEG:protected_mode

disk_error:
    mov si,msg_fail
    call print_string
    jmp $                      

print_string:
    mov ah,0x0E                
.next:
    lodsb                       
    test al,al                 
    jz .done
    int 0x10                   
    jmp .next
.done:
    ret

; ---- Global Descriptor Table (GDT) ----
gdt_start:
gdt_null:                      
    dq 0x0
gdt_code:                       ; Flat 4GB Code Segment
    dq 0x00CF9A000000FFFF   
gdt_data:                       ; Flat 4GB Data Segment
    dq 0x00CF92000000FFFF   
gdt_end:

gdt_descriptor:
    dw gdt_end-gdt_start-1  ; Size of GDT
    dd gdt_start            ; Start address of GDT

; ---- Data Area ----
msg_fail    db "DISK ERROR!",0
BOOT_DRIVE  db 0

; ---- 32-bit Protected Mode ----
bits 32
protected_mode:
    mov ax,DATA_SEG      
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax
    
    mov esp,0x90000      ; stack in high memory

    mov esi,0x00010000      ; source (loaded kernel)
    mov edi,0x00100000      ; destination (1 MB)
    mov ecx,KERNEL_SECTORS
    shl ecx,9               ; sectors → bytes (×512)
    rep movsb

    jmp KERNEL_LOAD_ADDR

; Boot sector padding
times 510 - ($-$$) db 0
dw 0xAA55
