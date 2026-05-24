dispatch_command:
    pushad
    mov esi,cmd_table
.next_cmd:
    cmp byte [esi],0
    je .not_found      ; no command
    mov ebx,esi        ; table string
    mov eax,[argc]
    test eax,eax
    jz .not_found
    mov edi,[argv]     ; argv[0] = command

.compare:
    mov al,[ebx]
    mov dl,[edi]
    cmp al,dl
    jne .skip_it
    test al,al
    jz .found
    inc ebx
    inc edi
    jmp .compare
.skip_it:
    cmp byte [esi],0
    je .after_name
    inc esi
    jmp .skip_it
.after_name:
    inc esi              ; skip 0
    add esi,4            ; skip dd handler
    jmp .next_cmd
.found:
.find_end:
    cmp byte [esi],0
    je .get_addr
    inc esi
    jmp .find_end
.get_addr:
    inc esi              ; get cmd address 
    mov eax,[esi]
    mov [cmd_exec],eax   
    popad
    mov eax,[cmd_exec]   
    call eax             ; execute command
    ret
.not_found:
    popad
    call exec_bin
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
    mov [prompt_limit],ebx    ; ebx from putch  
    ret

cursor:
    push eax
    push edx
    mov dx,0x3D4
    mov al,0x0F
    out dx,al
    mov dx,0x3D5
    mov al,byte[cursor_pos]
    out dx,al
    mov dx,0x3D4
    mov al,0x0E
    out dx,al
    mov dx,0x3D5
    mov al,byte[cursor_pos+1]
    out dx,al
    pop edx
    pop eax
    ret

;----- string,0 ------
; The oldest form of 
; kernel communication
;---------------------
print:
    push eax
.next:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .next
.done:
    pop eax
    ret

;---- string,13,0 ----
print_cr:
    push eax
.next:
    lodsb
    test al,al
    jz .done
    cmp al,13
    je .newln
    call putchar
    jmp .next
.newln:
    call newline
    jmp .next
.done:
    pop eax
    ret

; --------------------------------------
; Ego boost at boot - look what we built
; --------------------------------------
banner:
    mov esi,sys_msg
    mov edi,0xC00B8000+(2*80+0)*2
    mov ah,0x02
.next:
    lodsb
    test al,al
    jz .done
    stosw
    jmp .next
.done:
    ret

cls:                       
    mov edi,0xC00B8000     ; VGA text memory
    mov ecx,80*25/2        ; 80x25 characters
    mov eax,0x07200720     ; green+space
    rep stosd
    xor eax,eax
    mov [cursor_pos],eax
    call cursor
    ret

scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi,0xC00B8000+160  
    mov edi,0xC00B8000
    mov ecx,80*24       
.lp1:
    mov ax,[esi]
    mov [edi],ax
    add esi,2
    add edi,2
    loop .lp1
    mov ecx,80
    mov ax,0x0720       
.lp2:
    mov [edi],ax
    add edi,2
    loop .lp2
    pop eax
    pop ecx
    pop edi
    pop esi
    ret

;---- Read Key ----
get_key:
    mov eax,[kbd_tail]
    cmp eax,[kbd_head]
    je .empty
    mov bl,[kbd_buf+eax]
    inc eax
    and eax,255
    mov [kbd_tail],eax
    mov al,bl
    test al,al
    jz .empty
    ; -------------------------
    ; arrow up
    ; -------------------------
    cmp al,0x48
    jne .check_down
    call hist_back
    xor al,al
    ret
.check_down:
    ; -------------------------
    ; arrow down
    ; -------------------------
    cmp al,0x50
    jne .translate
    call hist_frwd
    xor al,al
    ret
.translate:
    cmp al,0x01
    je  shutdown         ;escape key
    ; reject invalid scancodes
    cmp al,128
    jae .invalid
    movzx eax,al
    ; -------------------------
    ; choose keymap
    ; -------------------------
    cmp byte [kbd_shift],0
    jne .shifted
.normal:
    mov al,[keymap+eax]
    ret
.shifted:
    mov al,[keymap_shift+eax]
    ret
.empty:
    xor al,al
    ret
.invalid:
    mov al,'?'
    ret

; --- The final curtain call    ---
; --- powering down the theater ---
shutdown:
    cli
    mov dx,0xF4
    mov al,0
    out dx,al
    hlt
    ret

    
