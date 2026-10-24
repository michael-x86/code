; ---------------------------
; Because forgetting is human 
; remembering is divine
; ---------------------------
save_history:
    pushad
    cmp dword [cmd_len],0
    je .done
    mov eax,[hist_count]
    cmp eax,32
    jb .append
    mov esi,hist_buf+64
    mov edi,hist_buf
    mov ecx,31*64
    rep movsb
    mov dword [hist_count],31
.append:
    mov eax,[hist_count]
    mov ebx,64
    mul ebx
    mov edi,hist_buf
    add edi,eax
    mov esi,cmd_buf
.copy:
    lodsb
    stosb
    test al,al
    jnz .copy
    inc dword [hist_count]
    mov eax,[hist_count]
    mov [hist_index],eax
.done:
    popad
    ret

hist_back:
    pushad
    mov eax,[hist_index]
    test eax,eax
    jz .done              ; already at oldest
    dec eax
    mov [hist_index],eax
    mov ebx,64
    mul ebx
    mov esi,hist_buf
    add esi,eax
    mov edi,cmd_buf
    xor ecx,ecx
.copy:
    lodsb
    stosb
    test al,al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len],ecx
    mov [cmd_pos],ecx     ; cursor at end of recalled line
    call redraw_line
.done:
    popad
    ret

hist_frwd:
    pushad
    mov eax,[hist_index]
    mov ebx,[hist_count]
    cmp eax,ebx
    jae .done
    inc eax
    mov [hist_index],eax
    cmp eax,ebx
    je .empty_line
    mov ecx,64
    mul ecx
    mov esi,hist_buf
    add esi,eax
    mov edi,cmd_buf
    xor ecx,ecx
.copy:
    lodsb
    stosb
    test al,al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len],ecx
    mov [cmd_pos],ecx
    call redraw_line
    popad
    ret
.empty_line:
    mov dword [cmd_len],0
    mov dword [cmd_pos],0
    call redraw_line
.done:
    popad
    ret
clear_cmdline:
    pushad
.loop:
    cmp dword [cmd_len],0
    jbe .done
    dec dword [cmd_len]
    call delchar
    jmp .loop
.done:
    popad
    ret


; -------------------------------------
; lookup table dispatcher
; -------------------------------------
; every command is a /bin executable.
dispatch_command:
    call exec_bin
    ret

; ---------------------------------------------------------------
;   Reads "exec.cmd" 
;   each line as a shell command, in order.
;   Invoked by the `run` command (syscall 50)/any int 0x80 caller.
; ---------------------------------------------------------------
sys_run:
    pushad
    mov esi,run_file             ; "exec.cmd"
    mov edi,resolve_buf
    call fs_resolve              ; resolve_buf = absolute path
    mov esi,resolve_buf
    call fs_lookup               ; eax = entry ptr or 0
    test eax,eax
    jz .nofile
    cmp dword [eax+FS_NAME_LEN],1    ; must be a regular file (type 1)
    jne .nofile
    mov esi,[eax+FS_NAME_LEN+4]      ; file data ptr
    mov ecx,[eax+FS_NAME_LEN+8]      ; file size
    mov [run_ptr],esi
    mov [run_rem],ecx
.next_line:
    mov ecx,[run_rem]
    test ecx,ecx
    jz .done
    mov esi,[run_ptr]
    mov edi,cmd_buf
    xor edx,edx                     ; line length so far
.chars:
    test ecx,ecx
    jz .eol
    mov al,[esi]
    inc esi
    dec ecx
    cmp al,10                       ; \n ends the line
    je .eol
    cmp al,13                       ; \r ends the line
    je .eol
    cmp edx,63                      ; cap the command line at 63 chars
    jae .chars
    mov [edi],al
    inc edi
    inc edx
    jmp .chars
.eol:
    mov byte [edi],0                ; null-terminate cmd_buf
    mov [run_ptr],esi               ; save cursor across the dispatch
    mov [run_rem],ecx
    test edx,edx
    jz .next_line                   ; skip blank lines
    call parse_args
    call check_bg
    call dispatch_command           ; runs it; "command not found" if unknown
    jmp .next_line
.nofile:
    mov esi,run_nofile_msg
    call print_cr
.done:
    popad
    xor eax,eax
    ret

delchar:
    mov ebx,[cursor_pos]
    cmp ebx,[prompt_limit]
    jbe .rt
    dec ebx
    cmp ebx,[prompt_limit]
    jb .rt
    mov edx,0xC00B8000
    lea edi,[edx+ebx*2]
    mov ax,0x0020   
    mov [edi],ax
    mov [cursor_pos],ebx
    call cursor
.rt:
    ret

putchar:
    push edi
    push edx
    push eax 
    mov ebx,[cursor_pos]
    mov edx,0xC00B8000
    lea edi,[edx+ebx*2]
    mov ah,0x02         ; color
    mov [edi],ax
    inc ebx
    cmp ebx,80*25
    jb .st
    call scroll
    mov ebx,80*24
.st:
    mov [cursor_pos],ebx
    call cursor
    pop eax
    pop edx
    pop edi
    ret

; ---------------------------------------------------------------
; cmd_buf holds the line, cmd_len its length,
; cmd_pos the cursor index. 
; The line is drawn starting at prompt_limit
; ---------------------------------------------------------------

; repaint the whole command line and place the hardware
; cursor at prompt_limit+cmd_pos. 
redraw_line:
    pushad
    mov ebx,[prompt_limit]
    mov edi,0xC00B8000
    lea edi,[edi+ebx*2]
    mov ecx,64
    mov ax,0x0720              ; blank?
.clear:
    mov [edi],ax
    add edi,2
    loop .clear
    mov ebx,[prompt_limit]
    mov edi,0xC00B8000
    lea edi,[edi+ebx*2]
    mov esi,cmd_buf
    mov ecx,[cmd_len]
    mov ah,0x02               ; same green attribute as putchar
.paint:
    test ecx,ecx
    jz .place
    mov al,[esi]
    mov [edi],ax
    add edi,2
    inc esi
    dec ecx
    jmp .paint
.place:
    mov eax,[prompt_limit]
    add eax,[cmd_pos]
    mov [cursor_pos],eax
    call cursor
    popad
    ret

; move only the hardware cursor to match cmd_pos.
; Got no software cursor = Breakouts "undocumented feature"
line_setcursor:
    push eax
    mov eax,[prompt_limit]
    add eax,[cmd_pos]
    mov [cursor_pos],eax
    call cursor
    pop eax
    ret

; insert AL at cmd_pos, shifting the tail right, then redraw.
line_insert:
    push eax
    push ecx
    push edx
    mov ah,al                 ; stash the char (al is reused below)
    mov ecx,[cmd_len]
    cmp ecx,63                ; leave room for the null terminator
    jae .ret
    mov edx,[cmd_pos]
.shift:
    cmp ecx,edx               ; move buf[i-1]->buf[i] for i=cmd_len..cmd_pos+1
    jle .put
    mov al,[cmd_buf+ecx-1]
    mov [cmd_buf+ecx],al
    dec ecx
    jmp .shift
.put:
    mov al,ah
    mov [cmd_buf+edx],al
    inc dword [cmd_len]
    inc dword [cmd_pos]
    call redraw_line
.ret:
    pop edx
    pop ecx
    pop eax
    ret

line_backspace:
    push eax
    push esi
    push edi
    mov esi,[cmd_pos]
    test esi,esi
    jz .ret                   ; nothing to the left of the cursor
    dec dword [cmd_pos]
    mov edi,[cmd_pos]         ; write index = cmd_pos-1
    mov esi,edi
    inc esi                   ; read index = cmd_pos
.shift:
    cmp esi,[cmd_len]
    jae .fin
    mov al,[cmd_buf+esi]
    mov [cmd_buf+edi],al
    inc esi
    inc edi
    jmp .shift
.fin:
    dec dword [cmd_len]
    call redraw_line
.ret:
    pop edi
    pop esi
    pop eax
    ret
    
newline: 
    push eax
    push ebx
    push edx
    mov eax,[cursor_pos]
    mov ebx,80
    xor edx,edx
    div ebx
    inc eax
    mul ebx
    mov [cursor_pos],eax
    cmp eax,80*25
    jb .st
    call scroll
    mov dword [cursor_pos],80*24  
.st:
    
    call cursor
    pop edx
    pop ebx
    pop eax
    ret

prompt:
    mov al,'$'   
    call putchar
    mov al,' '
    call putchar
    mov [prompt_limit],ebx   
    ret

cursor: 
    push eax
    push edx
    push ebx               
    mov bx,[cursor_pos]    
    mov dx,0x3D4
    mov al,0x0F
    out dx,al
    
    mov dx,0x3D5
    mov al,bl        
    out dx,al

    mov dx,0x3D4
    mov al,0x0E
    out dx,al
    
    mov dx,0x3D5
    mov al,bh        
    out dx,al

    pop ebx
    pop edx
    pop eax
    ret

