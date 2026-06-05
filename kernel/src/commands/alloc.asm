; alloc — allocate pages from the heap.  usage:  alloc <pagecount>
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

    ; --- Fetch page count argument ---
    GET_ARG 1, arg_buf
    cmp eax, -1
    je .usage

    ; Parse decimal page count
    lea esi, [ebp + arg_buf]
    PARSE_INT
    test ecx, ecx
    jz .usage

    ; --- Allocate pages via sys_alloc_pages ---
    mov eax, 23             ; sys_alloc_pages
    int 0x80
    cmp eax, -1
    je .failed

    ; Print result address
    lea esi, [ebp + ok_msg]
    SYS_PRINT

    mov ebx, eax
    SYS_PRINT_HEX

    SYS_NEWLINE
    ret

.failed:
    PRINT_ERROR err_msg

.usage:
    PRINT_USAGE usage_msg

usage_msg: db "usage: alloc <pagecount>", 13, 0
ok_msg:    db "allocated at 0x", 0
err_msg:   db "alloc: out of memory", 13, 0

section .bss
alignb 4
arg_buf: resb 32
