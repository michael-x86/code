; argtest — print each argument on its own line.  usage:  argtest [args...]
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

    ; Try to fetch arg 1
    mov ebx, 1
    lea edi, [ebp + buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .no_arg

    ; Print it
    lea esi, [ebp + buf]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

.no_arg:
    lea esi, [ebp + no_msg]
    mov eax, 2
    int 0x80
    ret

no_msg: db "(no arg 1)", 13, 0

section .bss
alignb 4
buf: resb 32
