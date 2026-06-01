; alloc — allocate pages from the heap.  usage:  alloc <pagecount>
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- Fetch page count argument ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; Parse decimal page count via sys_asc2int
    lea esi, [ebp + arg_buf]
    mov eax, 27             ; sys_asc2int
    int 0x80
    mov ecx, eax            ; ecx = page count
    test ecx, ecx
    jz .usage

    ; --- Allocate pages via sys_alloc_pages ---
    mov eax, 23             ; sys_alloc_pages
    int 0x80
    cmp eax, -1
    je .failed

    ; Print result address
    lea esi, [ebp + ok_msg]
    mov eax, 2              ; sys_print_cr — but we need raw print
    ; use sys_print for the message part
    push eax
    lea esi, [ebp + ok_msg]
    mov eax, 1              ; sys_print
    int 0x80
    pop eax

    mov ebx, eax
    mov eax, 5              ; sys_print_hex
    int 0x80

    mov eax, 3              ; sys_newline
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

usage_msg: db "usage: alloc <pagecount>", 13, 0
ok_msg:    db "allocated at 0x", 0
err_msg:   db "alloc: out of memory", 13, 0

section .bss
alignb 4
arg_buf: resb 32
