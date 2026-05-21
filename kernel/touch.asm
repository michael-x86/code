; touch - create an empty file.  usage:  touch <path>
[bits 32]
[org 0xC0700000]

%define arg  0xC0700400

_start:
    mov ebx, 1
    mov edi, arg
    mov eax, 14              ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    mov esi, arg
    mov eax, 17              ; sys_create
    int 0x80
    cmp eax, -1
    je .err
    ret

.usage:
    mov esi, usage_msg
    mov eax, 2
    int 0x80
    ret
.err:
    mov esi, err_msg
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: touch <path>", 13, 0
err_msg   db "touch: cannot create (exists or FS full)", 13, 0
