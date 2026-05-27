; cat - print file contents.  usage:  cat <path>
[bits 32]
[org 0x00000000]            ; Base is relative to whatever exec_bin allocates


_start:
    mov ebx, 1
    mov edi, arg
    mov eax, 14              ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    mov esi, arg
    mov edi, info
    mov eax, 15              ; sys_stat -> info: dword type, data, size
    int 0x80
    cmp eax, -1
    je .nf

    cmp dword [info], 0      ; type 0 = directory
    je .isdir

    mov esi, [info+4]
    mov ecx, [info+8]
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
    mov esi, usage_msg
    mov eax, 2
    int 0x80
    ret
.nf:
    mov esi, nf_msg
    mov eax, 2
    int 0x80
    ret
.isdir:
    mov esi, isdir_msg
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: cat <file>", 13, 0
nf_msg    db "cat: no such file", 13, 0
isdir_msg db "cat: is a directory", 13, 0
arg       times 64 db 0
info      times 3 dd 0
