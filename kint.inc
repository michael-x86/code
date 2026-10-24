; --------------------------------------------
; - Building the interrupt handler phonebook -
; --------------------------------------------
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

page_fault_isr:
    cli
    pushad
    mov eax,[esp+36]    ; 36=EIP ADDRESS  32=error
    push eax                
    call page_fault_handler
    add esp,4              
.Tom_Dooley:
    hlt
    jmp .Tom_Dooley

page_fault_handler:
    push ebp
    mov ebp,esp
    mov esi,pf_msg
    call print_cr    ;CR2
    mov esi,pf_addr
    call print
    ;mov eax,cr2
    call print_hex_dword
    call newline
    
    mov edx,0x3333       
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
sys_putchar:                 ; ebx = char
    mov eax,ebx
    call putchar
    xor eax,eax
    ret

sys_print:                   ; esi = ptr
    call print
    xor eax,eax
    ret

sys_print_cr:                ; esi = ptr
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

sys_print_hex:               ; ebx = value
    mov eax,ebx
    call print_hex_dword
    xor eax,eax
    ret

sys_print_int:               ; ebx = value
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
    mov esi, cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret

sys_chdir:     ; esi = path -> eax = 0 or -1
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

sys_list_dir:     ; (ls) ebx=index, edi=dst -> eax = type or -1
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
    call basename_if_child   ; eax = basename ptr(esi) or 0
    test eax, eax
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

sys_stat:          ; esi=path, edi=dst_info(12 bytes) -> eax=0/-1
    push esi
    push edi
    push ebx
    push edx
    mov ebx,edi              ; save dst_info
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    mov edx,[eax+FS_NAME_LEN]
    mov [ebx],edx
    mov edx,[eax+FS_NAME_LEN+4]
    mov [ebx+4], edx
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
    cmp al,0x09
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

sys_get_arg:         ; ebx = index out:  esi=dest 
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

sys_create:
    push esi
    push edi
    push ebx
    push ecx
    push edx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err

    mov edi,fs_entries
    mov ecx,FS_COUNT
.scan:
    test ecx,ecx
    jz .err
    cmp byte [edi],0
    je .got
    add edi,FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    ; copy resolved path into slot
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],1   ; type = file
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_write:           ; echo [text] > file
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1     ; must be regular file
    jne .err
    mov ecx,[esp+0]
    cmp ecx,FS_CAPACITY
    ja .err

    mov esi,[esp+4]                 ; caller's src buffer
    mov edi,[eax+FS_NAME_LEN+4]     ; entry's data ptr
    cld
    rep movsb
    mov ecx,[esp+0]
    mov [eax+FS_NAME_LEN+8],ecx     ; update size
    call persist_entry              ; eax = entry ptr still

    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

;  esi=path, ebx=buf, ecx=n -> append n bytes to an existing file.
sys_append:
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1     ; must be a regular file
    jne .err
    mov ebx,[eax+FS_NAME_LEN+8]       ; current size = append offset
    mov ecx,[esp+0]                   ; n
    mov edx,ebx
    add edx,ecx
    cmp edx,FS_CAPACITY               ; size + n must fit
    ja .err
    mov esi,[esp+4]                   ; src buffer
    mov edi,[eax+FS_NAME_LEN+4]       ; data ptr
    add edi,ebx                       ; + size
    cld
    rep movsb
    mov ecx,[esp+0]
    add ecx,ebx                       ; new size = old size+n
    mov [eax+FS_NAME_LEN+8],ecx
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

;   esi=path -> eax=0/-1
;   Marks slot free
;   so it can be reused by a future sys_create.
sys_unlink:
    push esi
    push edi
    push ebx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1
    jne .err
    mov byte  [eax],0               
    mov dword [eax+FS_NAME_LEN],0     
    mov dword [eax+FS_NAME_LEN+8],0  
    call persist_entry             
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_mkdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err                     ; already exists
    mov edi,fs_entries
    mov ecx,FS_COUNT
.scan:
    test ecx,ecx
    jz .err
    cmp byte [edi],0
    je .got
    add edi,FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],0   ; type = dir
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_rmdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],0    ; must be type dir
    jne .err
    push eax
    mov edi,resolve_buf              ; use as cwd for check
    mov esi,fs_entries
    mov ecx,FS_COUNT
.chk:
    test ecx,ecx
    jz .empty
    call basename_if_child
    test eax,eax
    jnz .notempty
    add esi,FS_REC_SIZE
    dec ecx
    jmp .chk
.notempty:
    pop eax
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret
.empty:
    pop eax                    ; eax = entry ptr
    mov byte [eax],0
    mov dword [eax+FS_NAME_LEN],0
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_ps_info:
    test ebx,ebx
    je .error
    push ecx
    push edx
    push esi
    push edi

    mov edx,[current_task]
    mov [ebx+0],edx
    mov dword [ebx+4],MAX_PROC

    xor ecx,ecx                 ; slot index
.slot:
    cmp ecx,MAX_PROC
    jae .done
    mov eax,ecx
    imul eax,PS_REC
    lea edi,[ebx+8]
    add edi,eax
    mov edx,[proc_state+ecx*4]
    mov [edi+0],edx
    mov edx,[proc_vbase+ecx*4]
    mov [edi+4],edx
    mov edx,[task_esps+ecx*4]
    mov [edi+8],edx
    mov eax,ecx
    shl eax,4                   ; *PROC_NAME_LEN (16)
    lea esi,[proc_name+eax]
    add edi,12
    push ecx
    mov ecx,PROC_NAME_LEN
.cpname:
    mov al,[esi]
    mov [edi],al
    inc esi
    inc edi
    loop .cpname
    pop ecx
    inc ecx
    jmp .slot
.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    mov eax,MAX_PROC
    ret
.error:
    mov eax,-1
    ret

sys_stack_dump:
    pushad
    mov edi,esp
    add edi,32           ; Skip past of PUSHAD registers!
    mov ecx,8
.loop:
    mov eax, [edi]       
    call print_hex_dword
    call newline
    add edi,4
    loop .loop
    popad
    ret             


; ---------------------------------------------
;  Memory dealer - handing out 4KB crack rocks
; ---------------------------------------------
sys_alloc:
    mov ecx,[argc]
    cmp ecx,2
    jne .usage
    mov esi,[argv+4]
    call sys_asc2int
    mov eax,edx
    test eax,eax
    jz .usage
    add eax,4095
    shr eax,12
    mov ecx,eax              ; page count
    push ecx
    call find_free_virt      ; eax = vbase, CF=1 on fail
    pop ecx
    jc .failed
    mov edi,eax              ; allocation base
    mov edx,eax              ; running vaddr
.alloc_loop:
    test ecx,ecx
    jz .done_alloc
    push edx
    push ecx
    call alloc_page          ; eax = phys
    pop ecx
    pop edx
    jc .failed
    mov ebx,eax              ; phys
    mov eax,edx              ; virt
    push edx
    push ecx
    mov ecx,3                ; flags = present|rw
    call map_page
    pop ecx
    pop edx
    jc .failed
    add edx,4096
    dec ecx
    jmp .alloc_loop
.done_alloc:
    mov eax,edi
    mov ebx,edx
    sub ebx,edi
    shr ebx,12               ; page count
    call register_allocation
    call newline
    mov esi,alloc_mem
    call print
    mov eax,edi
    call print_hex_dword
    call newline
    ret
.failed:
    mov esi,out_mem
    call print_cr
    ret
.usage:
    mov esi,no_arg
    call print_cr
    ret

