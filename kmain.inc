;-------------------------------
kernel_main:
    call build_idt
    call set_irq0
    call set_irq1
    call set_syscall
    lidt [idt_descriptor]
    call pic_remap
    call set_freq       
    call dydx_init
    call hwclock

    mov edi,cwd_buf
    mov ecx,128
    xor eax,eax
    rep stosb
    mov byte [cwd_buf],'/'

    call load_fs_persist
    call cls
    call login        
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax

    call init_tasks

    mov [task0_esp],esp 
    mov dword [current_task],0 
    sti   

.task0_entry:
    cmp byte [banner_mode],0  
    jne .skip
    cmp byte [gfx_mode],0      ; no banner while in graphics mode
    jne .skip
    cmp byte [mode13],0        ; also in mode 13h
    jne .skip
    cmp byte [tick_flag],0
    je .skip
    mov byte [tick_flag],0
    call sys_banner
    call sys_tick
    call sys_hertz
.skip:
    call get_key
    test al,al
    jz .done
    cmp al,13
    je .enter
    cmp al,8
    je .del
    cmp al,9
    je .tab
    cmp al,KEY_LEFT
    je .left
    cmp al,KEY_RIGHT
    je .right
    cmp al,KEY_UP
    je .up
    cmp al,KEY_DOWN
    je .down
    cmp al,32               
    jb .done
    call line_insert          ; insert char at the cursor
    jmp .done
.enter:
    mov ebx,[cmd_len]
    mov byte [cmd_buf+ebx],0
    call newline
    call save_history
    call parse_args
    call check_bg
    call dispatch_command
    mov dword [cmd_len],0     ; reset buffer
    mov dword [cmd_pos],0
    mov byte [cmd_buf],0
    call prompt
    jmp .task0_entry
.tab:
    call tab_complete
    jmp .done
.del:
    call line_backspace
    jmp .done
.left:
    cmp dword [cmd_pos],0
    jbe .done
    dec dword [cmd_pos]
    call line_setcursor
    jmp .done
.right:
    mov eax,[cmd_pos]
    cmp eax,[cmd_len]
    jae .done
    inc dword [cmd_pos]
    call line_setcursor
    jmp .done
.up:
    call hist_back
    jmp .done
.down:
    call hist_frwd
    jmp .done
.done:
    ; (polling) hlt might cause input lag or freeze.
    jmp .task0_entry

task1_entry:
    mov byte [tick_flag], 0
    inc dword [tick_div]
    test dword [tick_div], 1
    jnz .sleep
    mov byte [tick_flag], 1
.sleep:
    hlt      ; run when timer fires                 
    jmp task1_entry     

task2_entry:  
    test dword [on_off],1
    jnz .paused
    call dydx_step
.paused:
    hlt      ; run when state changes
    jmp task2_entry

sys_hertz:
    push eax
    push edi
    mov eax,[hz]
    mov edi,0xC00B8000+(1*80+59)*2  
    call int2str
    mov byte [edi],' '
    add edi,2
    mov byte [edi],'H'
    mov byte [edi+1],0x0C
    add edi,2
    mov byte [edi],'z'
    mov byte [edi+1],0x0C
    add edi,2
    mov byte [edi],' '
    mov byte [edi+1],0x00
    add edi,2
    mov byte [edi],' '
    mov byte [edi+1],0x00
    add edi,2

    pop edi
    pop eax
    ret
    
hwclock:
wait_cmos:
    mov al,0x0A
    out 0x70,al
    in  al,0x71
    test al,0x80
    jnz wait_cmos

    ; Years since 1970 * 365
    mov eax,[year]
    sub eax,1970
    mov ebx,365
    mul ebx
    mov esi,eax

    ; Leap years since 1970
    mov eax,[year]
    sub eax,1969
    xor edx,edx
    mov ebx,4
    div ebx
    add esi,eax

    mov eax,[year]
    sub eax,1901
    xor edx,edx
    mov ebx,100
    div ebx
    sub esi,eax

    mov eax,[year]
    sub eax,1601
    xor edx,edx
    mov ebx,400
    div ebx
    add esi,eax

    mov ecx,[month]
    cmp ecx,1
    jle .months_done
    dec ecx
    mov ebx,month_days
    .mloop:
    movzx eax,byte [ebx]
    add esi,eax
    inc ebx
    dec ecx
    jnz .mloop

.months_done:
    mov eax,[day]
    dec eax
    add esi,eax
    mov eax,esi
    mov ebx,86400
    mul ebx
    mov esi,eax
    mov eax,[hour]
    mov ebx,3600
    mul ebx
    add esi,eax
    mov eax,[min]
    mov ebx,60
    mul ebx
    add esi,eax
    mov eax,[sec]
    add esi,eax
    mov [boot_epoch], esi
    ret


bcd2bin:               ; not in use (I'll keep it anyway)
    push ebx
    mov bl,al
    shr al,4
    mov bh,10
    mul bh             ; AL=tens*10
    and bl,0x0F        ; BL=ones
    add al,bl          ; AL=binary result
    pop ebx
    ret

int2str:
    push ebx
    push ecx
    push edx
    push esi
    mov ebx,10
    xor ecx,ecx
    test eax,eax
    jnz .convert
    mov byte [edi],'0'
    add edi,2
    jmp .done
.convert:
.repeat:
    xor edx,edx
    div ebx             ; EAX=quotient EDX=remainder
    add dl,'0'
    push edx
    inc ecx
    test eax,eax
    jnz .repeat
.print:
    pop edx
    mov [edi],dl
    add edi,1
    mov byte [edi],0x0C
    add edi,1
    loop .print
.done:
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

;---  Parse ARGS  ---
parse_args:
    mov esi,cmd_buf
    xor ecx,ecx
    mov edi,argv
.skip_spaces:
    mov al,[esi]
    cmp al,' '
    jne .check
    inc esi
    jmp .skip_spaces
.check:
    test al,al
    jz .done
    mov [edi],esi
    add edi,4
    inc ecx
.scan:
    mov al,[esi]
    test al,al
    jz .done
    cmp al,' '
    je .term
    inc esi
    jmp .scan
.term:
    mov byte [esi],0
    inc esi
    jmp .skip_spaces
.done:
    mov [argc],ecx    ;out
    ret

;--- "cmd &" or "cmd&" ---
; clears the '&' from argv and sets bg_flag.
check_bg:
    mov dword [bg_flag],0
    mov ecx,[argc]
    test ecx,ecx
    jz .ret
    mov eax,ecx
    dec eax
    mov esi,[argv+eax*4]       ; last argument
    cmp byte [esi],'&'
    jne .check_suffix
    cmp byte [esi+1],0
    jne .check_suffix
    mov dword [bg_flag],1
    dec dword [argc]           ; drop the &
    ret
.check_suffix:
    ; cmd&
    mov edi,esi
.find_end:
    mov al,[edi]
    test al,al
    jz .test_amp
    inc edi
    jmp .find_end
.test_amp:
    cmp edi,esi                
    je .ret
    cmp byte [edi-1],'&'
    jne .ret
    mov byte [edi-1],0          ; strip the '&'
    mov dword [bg_flag],1
.ret:
    ret

