[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    ;lea esi,[ebp+help_lbl]
    
    mov eax,25               ; sys_dealloc
    int 0x80
    ret
