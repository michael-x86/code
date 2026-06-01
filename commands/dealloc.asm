; dealloc — free a previously allocated heap range.  usage:  dealloc <hexaddr>
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- Fetch address argument ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; Parse hex address via sys_hex2int
    lea esi, [ebp + arg_buf]
    mov eax, 26             ; sys_hex2int
    int 0x80

    ; --- Free pages via sys_free_pages ---
    mov ebx, eax
    mov eax, 24             ; sys_free_pages
    int 0x80
    cmp eax, -1
    je .failed

    lea esi, [ebp + ok_msg]
    mov eax, 2
    int 0x80
    ret

.failed:
    lea esi, [ebp + err_msg]
    mov eax, 2
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg: db "usage: dealloc <hexaddr>", 13, 0
ok_msg:    db "dealloc: freed successfully", 13, 0
err_msg:   db "dealloc: address not found in allocation table", 13, 0

section .bss
alignb 4
arg_buf: resb 32
