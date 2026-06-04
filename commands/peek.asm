; peek — read a byte from a memory address.  usage:  peek <hexaddr>
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START
    mov eax, 26             ; hexbyte
    int 0x80
    ret

usage_msg: db "usage: peek <hexaddr>", 13, 0

section .bss
alignb 4
arg_buf: resb 32
