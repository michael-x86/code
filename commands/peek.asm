[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    ;lea esi,[ebp+help_lbl]
    ;mov ebx,sys_peek_msg 
    ;mov eax,10               ; sys_read_mem
    ;int 0x80

    mov eax,26               ; sys_peek
    int 0x80
    ret
