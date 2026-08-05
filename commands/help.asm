[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    lea esi,[ebp+help_lbl]
    mov eax,2               ; sys_print_cr
    int 0x80
    ret

help_lbl:
        db 13,"There is no help but what we make",13,13
        db 0
