; touch - create an empty file.  usage:  touch <path>
;
; ABI contract: see pwd.asm header.
[bits 32]
[org 0x00000000]

%include "userland.inc"


_start:
    USERLAND_START

    mov ebx, 1
    lea edi, [ebp + arg]
    mov eax, 14              ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    lea esi, [ebp + arg]
    mov eax, 17              ; sys_create
    int 0x80
    cmp eax, -1
    je .err
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret
.err:
    lea esi, [ebp + err_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: touch <path>", 13, 0
err_msg   db "touch: cannot create (exists or FS full)", 13, 0

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
arg: times 128 db 0
