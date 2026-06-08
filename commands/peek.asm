[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    mov eax,26               ; sys_peek
    int 0x80
    mov eax,28               ; sys_peek
    int 0x80
    mov eax,2               ; sys_peek
    int 0x80
    ret
