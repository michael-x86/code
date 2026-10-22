build_idt:
    mov edi, idt_start
    mov ecx, 256
.fill:
    mov eax,isr_default
    mov [edi],ax                ; Offset low (0-15)
    mov word [edi+2],0x08       ; CODE_SEG 
    mov byte [edi+4],0
    mov byte [edi+5],10001110b  ; Present, Ring 0, 32-bit Interrupt Gate
    shr eax, 16
    mov [edi+6],ax              ; Offset high (16-31)
    add edi, 8
    loop .fill

    mov eax, page_fault_isr
    mov ebx, 14                 ; Page Fault vector index
    mov edi, idt_start
    call set_idt_entry
    ret

set_idt_entry:
    push eax
    push edx                 
    mov edx,eax
    mov word [edi+ebx*8+0],dx  ; offset low
    mov word [edi+ebx*8+2],0x08 
    mov byte [edi+ebx*8+4],0
    mov byte [edi+ebx*8+5],10001110b
    shr edx, 16
    mov word [edi+ebx*8+6],dx  ; offset high
    pop edx
    pop eax             
    ret

isr_default:
    pushad
    mov al,0x20
    out 0x20,al
    popad
    iretd

;----  IRQ 0 & 1  ----
set_irq0:
    mov eax,irq0
    mov edx,eax
    mov edi,idt_start+0x20*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr edx,16
    mov word [edi+6],dx
    ret 

set_irq1:
    mov eax,irq1
    mov edi,idt_start+0x21*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax,16
    mov word [edi+6],ax
    ret

; syscall (int 0x80) 
set_syscall:
    mov eax,syscall_isr
    mov edi,idt_start+0x80*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],11101110b   ; 32-bit int gate
    shr eax,16
    mov word [edi+6],ax
    ret

; ---------------------------------------------------------
; int 0x80 dispatcher
;   in : eax = syscall #
;        ebx,ecx,edx,esi,edi = args
;   out: eax = return value (-1 on bad #)
; ---------------------------------------------------------
;  The great gatekeeper of ring 0 
; ---------------------------------------------------------
syscall_isr:
    cmp eax,SYSCALL_COUNT
    jae .bad
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push ebp
    call [syscall_table+eax*4]
    pop ebp
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    iretd
.bad:
    mov eax,-1
    iretd

;  syscall handlers
irq0:
    pushad
    inc dword [tick_count]
    mov eax,[tick_count]
    xor edx,edx
    ;mov ebx,100
    mov ebx,[hz]
    div ebx
    test edx,edx
    jnz .skip_epoch
    inc dword [boot_epoch]
.skip_epoch:
    mov ecx,[current_task]
    mov [task_esps+ecx*4],esp  ; save outgoing task
    call next_task             ; ecx -> next active slot
    mov [current_task],ecx
    mov esp,[task_esps+ecx*4]  ; load incoming task
.done:
    mov al, 0x20
    out 0x20, al
    popad
    iretd

; -----------------------------------------------------------
; 800x400x32 graphics mode (Bochs/QEMU VBE dispi+linear FB)
; -----------------------------------------------------------
pic_remap:            
    pusha 

    in  al,0x21       
    mov ah,al       
    in  al,0xA1      
    push ax           

    mov al,0x11      
    out 0x20,al      
    out 0xA0,al      

    mov al,0x20      
    out 0x21,al
    mov al,0x28      
    out 0xA1,al

    mov al,0x04       
    out 0x21,al
    mov al,0x02      
    out 0xA1,al        

    mov al,0x01         ; 8086/88 (Intel) Mode
    out 0x21,al
    out 0xA1,al

    pop ax              ; AL = Slave mask, AH = Master mask
    out 0xA1,al         ; Restore Slave mask
    mov al,ah           ; Put Master mask back into AL
    out 0x21,al         ; Restore Master mask
    popa
    ret

;   set the PIT timer frequency from the command line.
;   Reads argv[1] (decimal Hz); clamps to 10..2000, default 100
;   Returns eax = Hz.
