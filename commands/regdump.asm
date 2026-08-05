[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base      

    mov eax,41              ; sys_reg_dump
    int 0x80

    lea esi,[ebp+cr_lbl] 
    mov eax,2               ; sys_print_cr
    int 0x80
    xor eax,eax
    ret

cr_lbl: db 13,0
