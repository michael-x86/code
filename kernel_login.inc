login:
    pushad
.attempt:
    mov esi,login_user_msg
    call print
    mov byte [login_echo],1     ; show the username
    mov edi,login_ubuf
    call login_readline

    mov esi,login_pass_msg
    call print
    mov byte [login_echo],0     ; mask the passwd with '*'
    mov edi,login_pbuf
    call login_readline

    mov esi,login_ubuf          ; eax=0 when equal
    mov edi,login_root
    call str_eq
    test eax,eax
    jnz .bad
    mov esi,login_pbuf
    mov edi,login_secret
    call str_eq
    test eax,eax
    jnz .bad

    mov esi,login_ok_msg
    call print_cr
    popad
    ret
.bad:
    mov esi,login_bad_msg
    call print_cr
    jmp .attempt

;  Echoes each char,
;  or '*' when [login_echo]=0. 
login_readline:
    push ebx
    push ecx
    xor ecx,ecx
.next:
    call login_getchar           
    test al,al
    jz .next
    cmp al,13
    je .done
    cmp al,8
    je .bs
    cmp al,32
    jb .next                       ; ignore other codes
    cmp ecx,63
    jae .next                      
    mov [edi+ecx],al
    inc ecx
    cmp byte [login_echo],0
    jne .echo
    mov al,'*'
.echo:
    call putchar
    jmp .next
.bs:
    test ecx,ecx
    jz .next
    dec ecx
    call login_del
    jmp .next
.done:
    mov byte [edi+ecx],0
    call newline
    pop ecx
    pop ebx
    ret

login_getchar:
    push ebx
.wait:
    in al,0x64
    test al,1                      ; output buffer full?
    jz .wait
    in al,0x60                     ; scancode
    mov bl,al
    cmp bl,0x2A
    je .son
    cmp bl,0x36
    je .son
    cmp bl,0xAA
    je .soff
    cmp bl,0xB6
    je .soff
    test bl,0x80                   ; key release -> ignore
    jnz .wait
    movzx eax,bl
    cmp byte [login_shift],0
    jne .sh
    mov al,[keymap+eax]
    jmp .ret
.sh:
    mov al,[keymap_shift+eax]
.ret:
    pop ebx
    ret
.son:
    mov byte [login_shift],1
    jmp .wait
.soff:
    mov byte [login_shift],0
    jmp .wait

login_del:
    push eax
    push edi
    mov eax,[cursor_pos]
    test eax,eax
    jz .ret
    dec eax
    mov [cursor_pos],eax
    mov edi,0xC00B8000
    lea edi,[edi+eax*2]
    mov word [edi],0x0720          ; blank cell (green attr)
    call cursor
.ret:
    pop edi
    pop eax
    ret

; --------------------------------------
; Ego boost at boot - look what we built
; --------------------------------------
