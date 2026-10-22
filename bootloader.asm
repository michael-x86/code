[org 0x7C00]

; Max kernel_sectors = 1024 (512 KB kernel)
KERNEL_SECTORS equ 346
BUFFER_SEG       equ 0x2000     ; Safe buffer zone (0x20000 linear)
KERNEL_LOAD_ADDR equ 0x00100000 ; Final destination (1 MB)

CODE_SEG equ 0x08
DATA_SEG equ 0x10

start:
    ;  Tvinga CS att bli 0x0000 för riktig hårdvara 
    jmp 0x0000:.clear_cs
.clear_cs:
    cli
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov sp, 0x7C00              ; Stack grows downwards from 0x7C00

    mov [BOOT_DRIVE], dl        

    ; ---- Enable A20 Line ----
    in al, 0x92
    or al, 00000010b
    out 0x92, al

    ; ---- Check INT 13h extensions ----
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc ext_error                
    cmp bx, 0xAA55              
    jne ext_error
    test cx, 1                  
    jz ext_error

    ; ---- Set up ES to -> sequential destination buffer ----
    mov ax, BUFFER_SEG
    mov es, ax
    xor di, di                  ; ES:DI = 0x2000:0000

    mov cx, KERNEL_SECTORS      ; Loop counter
.load_loop:
    push cx                     

    ; ---- DAP directly on the stack ----
    push dword 0                ; LBA upper 32-bits 
    push dword [LBA_START]      ; LBA lower 32-bits
    push es                     ; Dest Segment Pointer
    push di                     ; Dest Offset Pointer
    push word 1                 ; Counter 1 sector
    push word 0x0010            ; Size of packet (16 bytes)

    mov si, sp           

    mov ah, 0x42                ; Extended Read 
    mov dl, [BOOT_DRIVE]        
    int 0x13
    jc .disk_error_pop_stack    ; Skiter sig här måste vi rensa stacken!

    add sp, 16

    pop cx                      
    push cx                     
    cmp cx, KERNEL_SECTORS
    jne .skip_magic_check       

    push ds                     
    mov ax, BUFFER_SEG
    mov ds, ax

    ; Validate signature "KERN"
    cmp word [0x0003], 0x454B    
    je .first_ok
    
    ; Signaturen fel, återställ DS och rensa stacken innan felhantering
    pop ds
    add sp, 16              
    jmp disk_error

.first_ok:
    cmp word [0x0005], 0x4E52    
    je .sig_perfect
    
    pop ds
    add sp, 16
    jmp disk_error

.sig_perfect:
    pop ds                     
    jmp .skip_magic_check

.disk_error_pop_stack:
    add sp, 16                  
    pop cx                    
    jmp disk_error             

.skip_magic_check:
    ; ---- Advance Dest Pointers ----
    add di, 512                
    jnc .no_segment_carry      
    mov ax, es
    add ax, 0x1000             ; Bump Segment up by 64KB
    mov es, ax
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
    mov si, disk_err_msg
    call print_string
    jmp $

ext_error:
    mov si, ext_err_msg
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
    mov bh, 0x00
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

disk_err_msg db "Disk read error",13,10,0
ext_err_msg  db "INT13h LBA extensions not supported - cannot boot",13,10,0

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

LBA_START  dd 1     
BOOT_DRIVE db 0

; ---- 32-BIT PROTECTED MODE ----
bits 32
protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x90000       

    ; ---- Relocate Data from Buffer to 1 MB ----
    mov esi, 0x00020000         
    mov edi, KERNEL_LOAD_ADDR   
    mov ecx, KERNEL_SECTORS
    shl ecx, 7                  
    rep movsd                   

    jmp KERNEL_LOAD_ADDR       

; ---- PADDING ----
times 446 - ($-$$) db 0

db 0x80           ; Bootable flagga (Aktiv partition)
db 0x01,0x01,0x00 ; Cylinder 0, Head 1, Sector 1
db 0x7F           ; Partitions-typ (0x7F = Reserverad/Alternativ OS-miljö)
db 0xFE,0xFF,0xFF ; CHS-slut (Maxvärden)
dd 1              ; Start-LBA (Sektor 2, där kernel ligger!)
dd 20000          ; Totalt antal sektorer i partitionen 

; Partition 2,3 and 4 (empty, 3*16 bytes = 48 bytes)
times 48 db 0

dw 0xAA55      ; Detta hamnar nu exakt på byte 510 och 511 
