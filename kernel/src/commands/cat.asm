; cat - print file contents.  usage:  cat <path>
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
    lea edi, [ebp + info]
    mov eax, 15              ; sys_stat -> info: dword type, data, size
    int 0x80
    cmp eax, -1
    je .nf

    cmp dword [ebp + info], 0      ; type 0 = directory
    je .isdir

    mov esi, [ebp + info + 4]
    mov ecx, [ebp + info + 8]
    test ecx, ecx
    jz .empty
    mov eax, 16              ; sys_print_n
    int 0x80
    ; ensure cursor sits at start of next line if file didn't end with \n
    mov eax, 3
    int 0x80
    ret

.empty:
    ret
.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret
.nf:
    lea esi, [ebp + nf_msg]
    mov eax, 2
    int 0x80
    ret
.isdir:
    lea esi, [ebp + isdir_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: cat <file>", 13, 0
nf_msg    db "cat: no such file", 13, 0
isdir_msg db "cat: is a directory", 13, 0

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
arg:    times 64 db 0
info:   times 3  dd 0
