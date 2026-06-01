[bits 32]
[org 0xC0700000]

; -----    WORK TO BE DONE ----
_start:
    pushad
    mov [tick_count],eax               ;read
    mov [print_int_decimal],ecx        ;call
    mov [newline],ebx                  ;call
    mov [print],edx                    ;call
;----
    mov eax,[tick_count]
    mov ebx,100
    xor edx,edx
    div ebx

    push ebx
    mov ebx,[newline]
    call ebx
    pop ebx

    mov esi,uptime_msg
    push edx
    mov edx,[print]                    
    call edx
    pop edx

    push ecx
    mov ecx,[print_int_decimal]
    call ecx
    pop ecx
    
    mov esi,uptime_secs_msg
    push edx
    mov edx,[print]                    
    call edx
    pop edx

    mov eax,edx
    imul eax,10
    push ecx
    mov ecx,[print_int_decimal]
    call ecx
    pop ecx

    mov esi,uptime_ms_msg
    push edx
    mov edx,[print]                    
    call edx
    pop edx
    
    push ebx
    mov ebx,[newline]
    call ebx
    pop ebx

    popad
    ret

print_int_decimal dd 0,0  
newline           dd 0,0
print             dd 0,0
tick_count        dd 0

uptime_msg        db 'uptime: ',0
uptime_secs_msg   db ' seconds and ',0
uptime_ms_msg     db ' ms',0
