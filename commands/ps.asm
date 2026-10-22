[bits 32]
[org 0x00000000]

MAXP  equ 8         ; must match kernel MAX_PROC
REC   equ 24        ; must match kernel PS_REC (state+vbase+esp+name[16])
NAMEW equ 15        ; name column width

_start:
    pushad
    call .base
.base:
    pop ebp
    sub ebp, .base

    ; --- snapshot the kernel process table ---
    lea ebx,[ebp+ps_buffer]
    mov eax,22
    int 0x80
    cmp eax,-1
    je .done

    ; Print the column headers
    lea esi,[ebp+hdr]
    mov eax,2
    int 0x80

    xor ecx,ecx                 ; slot index (0 to MAXP-1)
.loop:
    cmp ecx,MAXP
    jae .done

    ; edx = record = ps_buffer + 8 + slot*REC
    mov eax,ecx
    imul eax,REC
    lea edx,[ebp+ps_buffer+8]
    add edx,eax
    
    mov eax, [edx]              ; Load the actual process state flag from kernel
    test eax, eax               ; state == 0 -> free slot, skip
    je .next

    ; --- PID ---
    mov ebx,ecx
    mov eax,6
    int 0x80
    lea esi,[ebp+sep]
    mov eax,2
    int 0x80

    ; --- name: slot 0 is idle, 1-2 fixed labels, 3+ snapshotted command ---
    cmp ecx,0
    je .lbl_idle
    cmp ecx,1
    je .lbl_hal
    cmp ecx,2
    je .lbl_init
    lea esi,[edx+12]            ; offset 12 contains the 16-byte name string
    jmp .pname

.lbl_idle:
    lea esi,[ebp+lbl0]          ; "idle"
    jmp .pname
.lbl_hal:
    lea esi,[ebp+lbl1]          ; "hal-9000"
    jmp .pname
.lbl_init:
    lea esi,[ebp+lbl2]          ; "init"

.pname:
    call print_name             ; prints esi padded to NAMEW
    lea esi,[ebp+sep]
    mov eax,2
    int 0x80

    ; --- address: base tasks show ESP (+8), spawned show vbase (+4) ---
    lea esi,[ebp+hexp]
    mov eax,2
    int 0x80
    mov ebx,[edx+4]             ; vbase
    cmp ecx,3
    jae .paddr
    mov ebx,[edx+8]             ; esp
.paddr:
    mov eax,5
    int 0x80
    lea esi,[ebp+sep]
    mov eax,2
    int 0x80

    ; --- state determination ---
    cmp ecx, 0                  ; Is this Slot 0?
    je .print_idle              ; Force slot 0 to show IDLE

    ; Since it bypassed 'state == 0', it's an active running background program
    lea esi, [ebp + st_run]     ; Show as RUNNING
    jmp .display

.print_idle:
    lea esi, [ebp + st_idle]    ; Show as IDLE

.display:
    mov eax, 2                  ; sys_print_string
    int 0x80

.next:
    inc ecx
    jmp .loop
.done:
    popad
    ret

; --- print esi (null-terminated) padded with spaces to NAMEW chars ---
print_name:
    mov edi,NAMEW
.pn_loop:
    mov al,[esi]
    test al,al
    jz .pn_pad
    movzx ebx,al
    mov eax,39                  ; sys_putchar
    int 0x80
    inc esi
    dec edi
    jnz .pn_loop
    ret
.pn_pad:
    test edi,edi
    jz .pn_done
    mov ebx,' '
    mov eax,39
    int 0x80
    dec edi
    jnz .pn_pad                 ; loop instruction omitted per architecture standards
.pn_done:
    ret

; --- Constant Data Area ---
hdr:      db "PID  NAME               ADDRESS      STATE",13,0
sep:      db "    ",0
hexp:     db "0x",0
lbl0:     db "init splash",0
lbl1:     db "skynet -d",0
lbl2:     db "cyberdyne",0
st_run:   db "RUNNING",13,0
st_sleep: db "SLEEPING",13,0
st_idle:  db "IDLE",13,0

; --- Uninitialized Data Area ---
section .bss
alignb 4
ps_buffer: resb 8 + MAXP*REC
