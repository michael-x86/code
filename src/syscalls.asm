; --------------------------------------------
; - Building the interrupt handler phonebook -
; --------------------------------------------
build_idt:
    mov edi,idt_start
    mov ecx,256
.fill:
    mov eax,isr_default
    mov [edi],ax             ; low
    mov word [edi+2],CODE_SEG 
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax,16
    mov [edi+6],ax           ; high
    add edi,8
    loop .fill

    mov eax,page_fault_isr
    mov ebx,14
    mov edi,idt_start
    call set_idt_entry
    ret

set_idt_entry:
    ; eax = handler address
    ; ebx = entry index
    ; edi = idt base

    mov edx, eax
    mov word [edi+ebx*8+0],dx        ; offset low
    mov word [edi+ebx*8+2],CODE_SEG  ; selector
    mov byte [edi+ebx*8+4],0
    mov byte [edi+ebx*8+5],10001110b 
    shr edx,16
    mov word [edi+ebx*8+6],dx        ; offset high
    ret

page_fault_isr:
    cli
    pushad
    mov eax,[esp+36]    ; 36=EIP address
    push eax                
    call page_fault_handler
    add esp,4              
.hang:
    hlt
    jmp .hang

page_fault_handler:
    push ebp
    mov ebp,esp
    mov esi,pf_msg
    call print_cr    
    mov esi,pf_addr
    call print
    call print_hex_dword
    call newline
    
    mov edx,0x6666       ; outer loop (adjust for time)
.outer:
    mov ecx,0xFFFF      
.inner:
    dec ecx
    jnz .inner
    dec edx
    jnz .outer

    pop ebp
    iret

isr_default:
    pushad
    mov al,0x20
    out 0x20,al
    popad
    iretd

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

;----  syscall (int 0x80)  ----
set_syscall:
    mov eax,syscall_isr
    mov edi,idt_start+0x80*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],11101110b   ; present, DPL=3, 32-bit int gate
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

; --- syscall handlers ---
sys_putchar:                 ; ebx=char
    mov eax,ebx
    call putchar
    xor eax,eax
    ret

sys_print:                   ; esi=ptr
    call print
    xor eax,eax
    ret

sys_print_cr:                ; esi=ptr
    call print_cr
    xor eax,eax
    ret

sys_newline:
    call newline
    xor eax,eax
    ret

sys_cls:
    call cls
    xor eax,eax
    ret

sys_print_hex:               ; ebx=value
    mov eax,ebx
    call print_hex_dword
    xor eax,eax
    ret

sys_print_int:               ; ebx=value
    mov eax,ebx
    call print_int_decimal
    xor eax,eax
    ret

sys_get_key:
    call get_key
    movzx eax,al
    ret

sys_get_tick:
    mov eax,[tick_count]
    ret

sys_shutdown:
    call shutdown
    ret

sys_read_mem:                ; ebx = virt addr -> eax = dword at [ebx]
    mov eax,[ebx]
    ret

sys_getcwd:                  ; edi = dst -> eax = 0
    push esi
    mov esi,cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret

sys_chdir:                   ; esi = path -> eax = 0 or -1
    push esi
    push edi
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],0   ; must be 0 (dir)
    jne .nf
    mov esi,resolve_buf
    mov edi,cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edi
    pop esi
    mov eax,-1
    ret

sys_list_dir:                ; ebx = index, edi = dst -> eax = type or -1
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov [tmp_dst],edi
    mov [tmp_left],ebx
    mov esi,fs_entries
    mov ecx,FS_COUNT
.next:
    test ecx,ecx
    jz .nf
    mov edi,cwd_buf
    call basename_if_child   ; eax = name ptr(esi) or 0
    test eax,eax
    jz .skip
    cmp dword [tmp_left],0
    je .hit
    dec dword [tmp_left]
.skip:
    add esi,FS_REC_SIZE
    dec ecx
    jmp .next
.hit:
    mov edi,[tmp_dst]
    push esi
    mov esi,eax
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    mov eax,[esi+FS_NAME_LEN]
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.nf:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov eax,-1
    ret

sys_stat:                   ; esi=path, edi=dst_info(12 bytes) -> eax=0/-1
    push esi
    push edi
    push ebx
    push edx
    mov ebx,edi             ; save dst_info
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    mov edx,[eax+FS_NAME_LEN]
    mov [ebx],edx
    mov edx,[eax+FS_NAME_LEN+4]
    mov [ebx+4],edx
    mov edx,[eax+FS_NAME_LEN+8]
    mov [ebx+8],edx
    pop edx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_print_n:                 ; esi=ptr, ecx=count -> eax=0
.lp:
    test ecx,ecx
    jz .done
    mov al,[esi]
    inc esi
    dec ecx
    cmp al,0x0A
    je .nl
    cmp al,0x0D
    je .lp                    ; skip CR
    cmp al, 0x09
    jne .raw
    mov al,' '
.raw:
    push ecx
    push esi
    call putchar
    pop esi
    pop ecx
    jmp .lp
.nl:
    push ecx
    push esi
    call newline
    pop esi
    pop ecx
    jmp .lp
.done:
    xor eax,eax
    ret

sys_get_arg:                 ; ebx = index, edi = dst -> eax = 0 or -1
    cmp ebx,[argc]
    jae .nf
    push esi
    mov esi,[argv+ebx*4]
    test esi,esi
    jz .nf_pop
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret
.nf_pop:
    pop esi
.nf:
    mov eax,-1
    ret
