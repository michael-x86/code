;   VGA mode 13h (320x200x256).
[bits 32]
[org 0x00000000]

_start:
    mov eax,46             ; sys_mode13  incl. 'bounus features' ;-)
    int 0x80
    mov eax,0     
    int 0x80
