[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    mov eax, 7              ; sys_get_key
    int 0x80
    test al,al
    
    mov eax,23               ; sys_peek
    int 0x80
    ret
