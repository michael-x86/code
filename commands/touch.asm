; touch - create an empty file.  usage:  touch <path>
[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base

    mov ebx, 1
    lea edi,[ebp+arg]
    mov eax, 14              ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    lea esi,[ebp+arg]
    mov eax, 17              ; sys_create
    int 0x80
    cmp eax, -1
    je .err
    ret

.usage:
    lea esi,[ebp+usage_msg]
    mov eax, 2
    int 0x80
    ret
.err:
    lea esi,[ebp+err_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: touch <path>", 13, 0
err_msg   db "touch: cannot create (exists or FS full)", 13, 0
arg       dd 0,0,0,0,0
