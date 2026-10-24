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
;    "Improved ;-)"  
; The oldest form of 
; kernel communication
;---------------------
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

; ---------------------------------------------------------
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
sys_banner:
    mov esi,sys_msg
    mov edi,0xC00B8000+(0*80+0)
    mov ah,0x03
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
    ;mov eax,0x07200720    ; green+space
    mov eax,0x0A200A20     
    rep stosd
    xor eax,eax
    mov eax,80*3
    mov [cursor_pos],eax
    call cursor
    ret

scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi,0xC00B8000+(80*4)*2
    mov edi,0xC00B8000+(80*3)*2
    mov ecx,80*23
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

; -----------------------------------------
; returns:
;   al = ascii char
;   al = 0 if no key
; -----------------------------------------
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

    ;xor ebx,ebx     ;key code
    ;movsx ebx,al
    ;mov eax,6
    ;int 0x80

    ; --- arrow keys: return values the shell loop handles them ---
    cmp al,0x4B            ; left
    jne .ck_right
    mov al,KEY_LEFT
    ret
.ck_right:
    cmp al,0x4D            ; right
    jne .ck_up
    mov al,KEY_RIGHT
    ret
.ck_up:
    cmp al,0x48            ; up
    jne .ck_down
    mov al,KEY_UP
    ret
.ck_down:
    cmp al,0x50            ; down
    jne .translate
    mov al,KEY_DOWN
    ret
.translate:
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
sys_tick:
    pushad
    mov eax,[tick_count]
    mov edi,0xC00B8000+(1*80+71)*2  
    mov ecx,8
.next:
    mov edx,eax
    shr edx,28
    and dl,0x0F
    add dl,'0'
    cmp dl,'9'
    jbe .store
    add dl,7
.store:
    mov dh,0x07
    mov dh,0x0C
    mov dh,0x07
    mov [edi],dx
    shl eax,4
    add edi,2
    dec ecx
    jnz .next
    popad
    ret

init_tasks:
    mov eax,task1_stack_top
    mov edx,task1_entry
    call seed_frame
    mov [task1_esp],eax

    mov eax,task2_stack_top
    mov edx,task2_entry
    call seed_frame
    mov [task2_esp],eax
    ret

