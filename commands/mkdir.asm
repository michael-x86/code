[bits 32]
[org 0xC0700000]

_start:
    mov ebx, 1
    mov edi, arg
    mov eax, 14                 ; sys_get_arg(1, arg)
    int 0x80
    cmp eax, -1
    je .usage

    mov esi, arg
    mov eax, 20                 ; sys_mkdir(esi) -> 0/-1
    int 0x80
    cmp eax, -1
    jne .done

    mov esi, err_mkdir
    mov eax, 2
    int 0x80
    ret
.usage:
    mov esi, usage_mkdir
    mov eax, 2
    int 0x80
.done:
    ret

err_mkdir:   db "mkdir: cannot create directory", 13, 0
usage_mkdir: db "usage: mkdir <name>", 13, 0
arg:         times 128 db 0

