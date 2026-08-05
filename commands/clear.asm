; clear - clear the VGA mode 13h framebuffer to black.  usage: clear
;   Ignored outside mode 13h.
[bits 32]
[org 0x00000000]

_start:
    mov eax,49             ; sys_gclear
    int 0x80
    ret
