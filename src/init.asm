kernel_main:
    call build_idt
    call set_irq0
    call set_irq1
    call set_syscall
    lidt [idt_descriptor]
    call pic_remap
    call set_freq            ; 100 hz.

    mov edi,cwd_buf
    mov ecx,128
    xor eax,eax
    rep stosb
    mov byte [cwd_buf],'/'

    call load_fs_persist
    call cls
    call banner
    mov eax,80*4
    mov [cursor_pos],eax
    call newline
    call prompt

    call init_tasks
    sti 
    mov [task0_esp],esp
    mov esp, [task0_esp]
    mov dword [current_task], 0
    popad
    iretd           ; jumps to task0_entry

; task 0
; Zombie task that mostly sleeps and waits for glory
; -------------------------------------
task0_entry:
    .loop:
    ;call task0_stuff
    hlt
    jmp .loop

; task 1
; Another sleeping beauty waiting for a scheduler kiss
; -------------------------------------
task1_entry:
    .loop:
    ;call task1_stuff
    hlt
    jmp .loop

; -------------------------------------
;       The real worker bee - 
;   shell and command processor 
; -------------------------------------
task2_entry:  
main_loop:
    cmp byte [tick_flag],0
    je .skip
    mov byte [tick_flag],0
    call print_tick
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
    call putchar
    mov ebx,[cmd_len]
    cmp ebx,63
    jae .done
    mov [cmd_buf+ebx],al
    inc ebx
    mov [cmd_len],ebx
    jmp .done
.enter:
    mov ebx,[cmd_len]         
    mov byte [cmd_buf+ebx],0
    call newline
    call save_history
    call parse_args
    call dispatch_command
    mov dword [cmd_len],0     ;reset buffer
    mov byte [cmd_buf],0
    call prompt
    hlt
    jmp main_loop
.tab:
    call tab_complete
    xor al,al
    jmp .done
.del:
    cmp dword [cmd_len],0
    jbe .done
    dec dword [cmd_len]
    call delchar
.done:
    hlt
    jmp main_loop

