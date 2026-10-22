shutdown:
    call cls
    call login     
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    ret
    ;cli
    ;mov dx,0xF4
    ;mov al,0
    ;out dx,al
    ;hlt

; --- print 8 hex digits at top-right ----
; Counting heartbeats of the system clock
sys_hex2int:
    xor eax,eax
    xor ebx,ebx
    mov bl,[esi]
    cmp bl,'0'
    jne .parse
    cmp byte [esi+1],'x'
    je .skip_prefix
    cmp byte [esi+1],'X'
    jne .parse
.skip_prefix:
    add esi,2
.parse:
    movzx ebx,byte [esi]
    test bl,bl
    jz .done_hex
    cmp bl,'0'
    jb .done_hex
    cmp bl,'9'
    jle .digit
    cmp bl,'A'
    jb .lower
    cmp bl,'F'
    jle .upper
    cmp bl,'a'
    jb .done_hex
    cmp bl,'f'
    ja .done_hex
.lower:
    sub bl,'a'-10
    jmp .shift
.upper:
    sub bl,'A'-10
    jmp .shift
.digit:
    sub bl,'0'
.shift:
    shl eax,4
    or eax,ebx
    inc esi
    jmp .parse
.done_hex:
    ret

   
;--------------------
; in:            
;   esi -> string 
; out:            
;   edx integer          
;--------------------   
sys_asc2int:             
    xor edx, edx
.loop: 
    movzx eax,byte[esi]
    test al,al
    jz .done
    cmp al,' '
    je .done
    sub eax,'0'
    imul edx,edx,10
    add edx,eax      
    inc esi
    jmp .loop
.done:
    ret

;----------------------------
; in:
;   eax = page addr
;   marks page allocated
;----------------------------
;     Marking territory - 
; "this page belongs to us"
;----------------------------
sys_mem_dump:
    pushad
    call newline
    mov ecx,64        ; total bytes
    mov edx,16        ; bytes per line
.loop:
    lodsb            
    call sys_hex_byte
    mov al,32  
    call putchar
    dec edx
    jnz .skip_nl
    call newline
    mov edx,16
.skip_nl:
    loop .loop
    popad
    ret

;----------------------------
; in:  
;  eax=unsigned 32-bit 
;----------------------------      
sys_reg_dump:
    pushad
    mov ebp,esp
    call newline
    mov esi,eax_lbl
    call print
    mov eax,[ebp+28]
    call print_hex_dword
    call newline
    mov esi,ecx_lbl
    call print
    mov eax,[ebp+24]
    call print_hex_dword
    call newline
    mov esi,edx_lbl
    call print
    mov eax,[ebp+20]
    call print_hex_dword
    call newline
    mov esi,ebx_lbl
    call print
    mov eax,[ebp+16]
    call print_hex_dword
    call newline
    mov esi,esp_lbl
    call print
    mov eax,[ebp+12]
    call print_hex_dword
    call newline
    mov esi,ebp_lbl
    call print
    mov eax,[ebp+8]
    call print_hex_dword
    call newline
    mov esi,esi_lbl
    call print
    mov eax,[ebp+4]
    call print_hex_dword
    call newline
    mov esi,edi_lbl
    call print
    mov eax,[ebp+0]
    call print_hex_dword
    call newline
    call newline
    popad
    ret

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

sys_peek:
    push eax 
    push esi
    mov eax,[argc]
    cmp eax,2
    jl .arguse
    mov esi,[argv+4]
    call sys_hex2int   ;in esi {str} out eax {int}
    call newline
    mov esi,eax 
    mov ecx,4
.loop:
    lodsb
    call sys_hex_byte
    mov al,' '
    call putchar
.cont:
    loop .loop
    call newline
    call newline
.done:
    pop esi
    pop eax 
    ret
.arguse:
    mov esi,peek_msg
    call print_cr
    pop esi
    pop eax 
    ret


sys_poke:
    push eax 
    push esi
    mov eax,[argc]
    cmp eax,3
    jl .usage
    mov esi,[argv+4]
    call sys_hex2int  
    mov edi,eax
    mov esi,[argv+8]
    call sys_hex2int   
    mov [edi],eax 
.done:
    pop esi
    pop eax 
    ret
.usage:
    mov esi,poke_msg
    call print_cr
    pop esi
    pop eax 
    ret

sys_plot:
    pushad
    mov eax,[argc]
    cmp eax,3      
    jl .usage
    mov esi,[argv+4]
    call sys_asc2int  
    mov ecx,edx
    cmp eax,80
    jge .usage

    mov esi,[argv+8]
    call sys_asc2int

    mov eax,edx
    mov edx,ecx
    mov ecx,eax

    imul ecx,80         
    add ecx,edx   
    shl ecx,1            
    mov edi,0xC00B8000
    add edi,ecx
    mov byte [edi],0xDB
    inc edi
    mov byte [edi],0x06 
    popad
    ret
.usage:
    mov esi,plot_msg
    call print_cr
    popad
    ret

sys_epoch:
    mov eax,[boot_epoch]   
    call print_dec
    mov eax,3
    int 0x80
    ret

;--------------------------------------
; in: 
;     ebx = target PID to terminate
; out: 
;     eax = 0 on success, -1 on failure
;--------------------------------------
sys_banner_flip:
    call cls
    xor dword [banner_mode],1
    ret

; -------------------------------------------
; All work and no play makes Jack a dull boy
; -------------------------------------------
deadbeef  db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

eax_lbl db "EAX: ",0
ebx_lbl db "EBX: ",0
ecx_lbl db "ECX: ",0
edx_lbl db "EDX: ",0
esi_lbl db "ESI: ",0
edi_lbl db "EDI: ",0
ebp_lbl db "EBP: ",0
esp_lbl db "ESP: ",0

