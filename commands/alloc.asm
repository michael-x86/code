[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    ;lea esi,[ebp+help_lbl]
    
    mov eax,24               ; sys_alloc
    int 0x80
    mov ebx,0          ; Return code 0 (Success)
    mov eax,0          ; System call number 0 (sys_exit)
    int 0x80           ; Trigger kernel interrupt

