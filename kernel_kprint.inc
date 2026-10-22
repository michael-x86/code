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
sys_hex_byte:   
    push eax
    shr al,4
    and al,0x0F
    call print_hex_nibble
    mov al,byte [esp]
    and al,0x0F
    call print_hex_nibble
    pop eax
    ret

print_hex_nibble:
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    ret

print_hex_dword:
    push eax
    push ecx
    mov ecx,8
.hloop:
    rol dword [esp+4],4
    mov al,byte [esp+4]
    and al,0x0F
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

sys_out_hex:
    push eax
    push ecx
    mov ecx,8
.hloop:
    rol dword [esp+4],4
    mov al,byte [esp+4]
    and al,0x0F
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

print_int_decimal:
    push eax
    push ebx
    push ecx
    push edx
    mov ecx,0
    mov ebx,10
    test eax,eax
    jnz .divide
    mov al,'0'    ; special case: zero
    call putchar
    jmp .fin
.divide:
    xor edx,edx
    div ebx       ; eax = quotient, edx = remainder
    push edx      ; save digit (0-9)
    inc ecx
    test eax,eax
    jnz .divide
.emit:
    pop edx
    mov al,dl
    add al,'0'
    call putchar
    loop .emit
.fin:
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret


print_dec:
    push eax
    push ebx
    push ecx
    push edx
    push edi
    lea edi,[dec_buf+10]
    mov byte [edi],0
    mov ebx,10
.loop:
    dec edi
    xor edx,edx
    div ebx
    add dl,'0'
    mov [edi],dl
    test eax,eax
    jnz .loop
.print:
    mov al,[edi]
    test al,al
    jz .done
    movzx ebx,al
    mov eax,39      ; sys_putchar
    int 0x80
    inc edi
    jmp .print
.done:

    pop edi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret


;-----------------------------------------------------
; Persistence layout: FS region starts at LBA 256.
; Each spare slot occupies SECTORS_PER_SLOT sectors.
;   sector 0 — 68-byte fs_entry record, zero-padded
;   sectors 1..4 — FS_CAPACITY (2048) bytes of content
;-----------------------------------------------------
sys_msg   db "----------------------------------------"
          db "----------------------------------------"
          db " *** x86 Operating System (EFBEADDE) ***"
          db "       Core timer:                      "
          db "----------------------------------------"
          db "----------------------------------------",0
