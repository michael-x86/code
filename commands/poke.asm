; poke — write a byte to a memory address.  usage:  poke <hexaddr> <hexbyte>
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

    lea esi, [ebp + arg_buf]
    mov eax, 26             ; sys_hex2int
    int 0x80
    mov [ebp + saved_addr], eax  ; save target address

    ; --- Fetch byte value argument ---
    mov ebx, 2
    lea edi, [ebp + arg_buf2]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    lea esi, [ebp + arg_buf2]
    mov eax, 26             ; sys_hex2int
    int 0x80                ; eax = byte value

    ; --- Write byte via sys_write_mem ---
    mov ebx, [ebp + saved_addr]  ; ebx = address
    mov ecx, eax            ; ecx = byte value
    mov eax, 25             ; sys_write_mem
    int 0x80
    cmp eax, -1
    je .failed

    ret

.failed:
    lea esi, [ebp + err_msg]
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg: db "usage: poke <hexaddr> <hexbyte>", 13, 0
err_msg:   db "poke: write failed", 13, 0

section .bss
alignb 4
arg_buf:    resb 32
arg_buf2:   resb 32
saved_addr: resd 1
