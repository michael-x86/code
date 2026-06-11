[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base      

    mov eax,23              ; sys_stack_dump
    int 0x80
    
    mov eax,3               ; sys_newline
    int 0x80

    mov ebx,0          ; Return code 0 (Success)
    mov eax,0          ; System call number 0 (sys_exit)
    int 0x80           ; Trigger kernel interrupt
