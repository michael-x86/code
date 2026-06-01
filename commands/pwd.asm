; pwd - print working directory
[bits 32]
[org 0x00000000]


_start:
    mov edi, buf
    mov eax, 11              ; sys_getcwd(edi)
    int 0x80

    mov esi, buf
    mov eax, 1               ; sys_print
    int 0x80
    mov eax, 3               ; sys_newline
    int 0x80
    ret

buf: times 128 db 0
