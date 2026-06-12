[bits 32]
[org 0x00000000]

_start:
    pushad
    call .get_base
.get_base:
    pop ebp                 ; ebp = absolute runtime address of .get_base
    sub ebp, .get_base      ; ebp = runtime delta address

    mov eax,30
    int 0x80   
    popad
    mov ebx,0          ; Return code 0 (Success)
    mov eax,0          ; System call number 0 (sys_exit)
    int 0x80           ; Trigger kernel interrupt

    ;ret
