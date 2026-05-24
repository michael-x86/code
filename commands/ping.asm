[bits 32]
[org 0xC0700000]

_start:
    mov esi,msg
    mov eax,2           ; sys_print_cr
    int 0x80

    ;mov eax,8           ; sys_get_tick -> eax
    ;int 0x80
    ;mov ebx,eax
    ;mov esi,tick_msg
    ;mov eax,1           ; sys_print
    ;int 0x80
    ;mov eax,5           ; sys_print_hex (ebx)
    ;int 0x80
    mov eax,3           ; sys_newline
    int 0x80
    ret

msg:      
      db 13,"PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.",13   
      db "64 bytes from 8.8.8.8: icmp_seq=1 ttl=116 time=7.11 ms",13
      db "64 bytes from 8.8.8.8: icmp_seq=2 ttl=116 time=7.36 ms",13,0

tick_msg db "tick=0x", 0
