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

;----- string,0 ------
; The oldest form of 
; kernel communication
;---------------------
echo_cmd:
    mov eax,[argc]
    cmp eax,2
    jl .done
    mov esi,[argv+4]
.print:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .print
.done:
    call newline
    ret

;------------------------------------
; out:     
;  pointers to malloc street
;------------------------------------
; Real estate agent for malloc street
;------------------------------------
;   snapshot heap allocation table for heap.
;   ebx = dst buffer (needs 8 + alloc_table_count*4 bytes)
;   [dst+0]        = free heap bytes
;   [dst+4]        = number of active allocations (n)
;   [dst+8 + i*4]  = base address of each active allocation (i = 0..n-1)
;   returns eax = n, or -1 if ebx is null
tab_complete:
    pushad
    mov ecx,[cmd_len]
    test ecx,ecx
    jz .out

    xor edx,edx                   ; word_start
    xor ebx,ebx
.ws:
    cmp ebx,ecx
    jae .ws_done
    cmp byte [cmd_buf+ebx],' '
    jne .ws_n
    lea edx,[ebx+1]
.ws_n:
    inc ebx
    jmp .ws
.ws_done:
    lea eax,[cmd_buf+edx]
    mov [tab_pfx],eax    ; prefix pointer
    mov eax,ecx
    sub eax,edx
    mov [tab_pfxlen],eax ; prefix length
    xor eax,eax
    test edx,edx
    setne al             ; mode 1 = argument (word_start > 0), 0 = command
    mov [tab_mode],al

    ; pass 1: count matches 
    mov byte [tab_printing],0
    call tab_scan
    mov edx,[tab_match_count]
    test edx,edx
    jz .out
    cmp edx,1
    je .do_complete

    ; --- multiple matches
    call newline
    mov byte [tab_printing],1
    call tab_scan
    call newline
    call prompt
    call redraw_line
    jmp .out

.do_complete:
    ; append the part of the match
    mov esi,[tab_single_ptr]
    add esi,[tab_pfxlen]
.cp:
    mov al,[esi]
    test al,al
    jz .cp_sp
    mov ebx,[cmd_len]
    cmp ebx,62
    jae .fin
    mov [cmd_buf+ebx],al
    inc dword [cmd_len]
    inc esi
    jmp .cp
.cp_sp:
    mov ebx,[cmd_len]            
    cmp ebx,63
    jae .fin
    mov byte [cmd_buf+ebx],' '
    inc dword [cmd_len]
.fin:
    mov eax,[cmd_len]
    mov [cmd_pos],eax
    call redraw_line
.out:
    popad
    ret

tab_scan:
    mov dword [tab_match_count],0
    mov dword [tab_single_ptr],0
    cmp byte [tab_mode],0
    jne .args

    ; /bin executables 
    mov esi,fs_entries
    mov ecx,FS_COUNT
.fe:
    test ecx,ecx
    jz .ret
    push esi
    push ecx
    call tab_basename
    test eax,eax
    jz .fe_next
    cmp dword [esi+FS_NAME_LEN],2
    jne .fe_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .fe_next
    call tab_hit
.fe_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .fe
.ret:
    ret

    ; argument mode
.args:
    mov esi,fs_entries
    mov ecx,FS_COUNT
.ae:
    test ecx,ecx
    jz .ret2
    push esi
    push ecx
    mov edi,cwd_buf
    call basename_if_child    
    test eax,eax
    jz .ae_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .ae_next
    call tab_hit
.ae_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .ae
.ret2:
    ret

; record/print a match. esi=null-terminated name.
tab_hit:
    inc dword [tab_match_count]
    mov [tab_single_ptr],esi
    cmp byte [tab_printing],0
    je .ret
    push esi
    push eax
.pr:
    mov al,[esi]
    test al,al
    jz .sp
    call putchar
    inc esi
    jmp .pr
.sp:
    mov al,' '
    call putchar
    call putchar
    pop eax
    pop esi
.ret:
    ret

tab_prefix_match:
    push esi
    push edi
    push ecx
    mov edi,[tab_pfx]
    mov ecx,[tab_pfxlen]
    test ecx,ecx
    jz .match             ; empty prefix match all
.cmp_loop:
    mov al,[esi]
    mov ah,[edi]
    cmp al,ah
    jne .no
    test al,al
    jz .match             ; both ended simultaneously
    inc esi
    inc edi
    dec ecx
    jnz .cmp_loop
.match:
    pop ecx
    pop edi
    pop esi
    mov eax,1
    ret
.no:
    pop ecx
    pop edi
    pop esi
    xor eax,eax
    ret

tab_basename:
    push esi
    ; check "/bin/" prefix
    cmp byte [esi],'/'
    jne .no
    cmp byte [esi+1],'b'
    jne .no
    cmp byte [esi+2],'i'
    jne .no
    cmp byte [esi+3],'n'
    jne .no
    cmp byte [esi+4],'/'
    jne .no
    ; make sure there's something after it and no further '/'
    lea esi, [esi+5]
    cmp byte [esi],0
    je .no
    mov eax,esi                  ; name start
.scan:
    mov al,[esi]
    test al,al
    jz .ok
    cmp al,'/'
    je .no                       ; nested dir skip
    inc esi
    jmp .scan
.ok:
    mov eax,[esp]                ; restore esi from stack
    lea eax,[eax+5]              ; /bin/ offset
    pop esi
    ret
.no:
    pop esi
    xor eax,eax
    ret


; --------------------------------------------
; - Building the interrupt handler phonebook -
; --------------------------------------------
