; CMOS RTC - read system time and compute Unix epoch
hwclock:
wait_cmos:
    mov al,0x0A
    out 0x70,al
    in  al,0x71
    test al,0x80
    jnz wait_cmos
    mov eax,[year]
    sub eax,1970
    mov ebx,365
    mul ebx
    mov esi,eax
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

bcd2bin:
    push ebx
    mov bl,al
    shr al,4
    mov bh,10
    mul bh
    and bl,0x0F
    add al,bl
    pop ebx
    ret
