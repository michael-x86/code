; cd - change directory. usage:  cd <path>   or   cd  (goes to /root)
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp                 
    sub ebp, .get_base    

    mov ebx,1
    lea edi,[ebp+.arg]
    mov eax, 14              ; sys_get_arg(ebx, edi) -> eax = len or -1
    int 0x80
    cmp eax,-1
    je .home

    lea esi,[ebp+.arg]
    mov eax, 12              ; sys_chdir(esi) -> 0 or -1
    int 0x80
    cmp eax, -1
    jne .done
    lea esi,[ebp+.err]
    mov eax, 2               ; sys_print_cr
    int 0x80
    ret

.home:
    lea esi,[ebp+.home_path]
    mov eax, 12
    int 0x80
.done:
    ret

.err:       db "cd: no such directory", 13, 0
.home_path: db "/root", 0
.arg:       times 64 db 0
