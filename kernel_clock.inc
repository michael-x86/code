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

sys_setfreq:
    push ecx
    push edx
    push esi

    mov edx,100
    mov eax,[argc]
    cmp eax,2
    jl .set_pit

    mov esi,[argv+4]
    call sys_asc2int 

    cmp edx,10
    jl .use_default
    cmp edx,2000
    jg .use_default
    jmp .set_pit
.use_default:
    mov edx,100
.set_pit:
    mov [hz],edx
    mov ecx,edx
    mov eax,1193182
    xor edx,edx
    div ecx                      ; eax  divisor=1193182/hz
    mov ecx,eax
    mov al,0x36
    out 0x43,al
    mov al,cl                    ; divisor low byte
    out 0x40,al
    mov al,ch                    ; divisor high byte
    out 0x40,al
    mov eax,[hz]                 ; return the frequency set
    pop esi
    pop edx
    pop ecx
    ret

;   VGA mode 13h (mode13 command). 
;   F5 or § returns to text mode?
set_freq:
    mov al,0x36
    out 0x43,al
    mov ax,11931        ; al=0x9B (LSB),ah=0x2E (MSB)
    out 0x40,al         
    mov al,ah         
    out 0x40,al       
    ret

