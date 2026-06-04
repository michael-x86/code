; exit — terminate the running shell (syscall 9: shutdown)
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    mov eax, 9              ; sys_shutdown
    int 0x80
    ret                     ; should never reach here
