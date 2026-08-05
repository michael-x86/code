[bits 32]
[org 0x00000000]

_start:
    pushad
    call .get_base
.get_base:
    pop ebp                 ; ebp = absolute runtime address of .get_base
    sub ebp, .get_base      ; ebp = runtime delta address

    mov eax,45
    int 0x80   
    popad
    ret
