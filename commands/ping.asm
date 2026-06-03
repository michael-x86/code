; ping — fake ping (no actual ICMP). prints canned response.
;
; ABI contract: see pwd.asm header.
[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    lea esi, [ebp + msg]
    mov eax, 2              ; sys_print_cr
    int 0x80

    mov eax, 3              ; sys_newline
    int 0x80
    ret

msg:
    db 13, "PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.", 13
    db "64 bytes from 8.8.8.8: icmp_seq=1 ttl=116 time=7.11 ms", 13
    db "64 bytes from 8.8.8.8: icmp_seq=2 ttl=116 time=7.36 ms", 13, 0
