[bits 32]
[org 0x00000000]

GLOBAL _start

SECTION .text

_start:
    ;  (500,500) on a 1920x1080 screen.
    
    ; Formula: Offset= (Y*Pitch)+(X*BytesPerPixel)
    mov ecx,500          ; Y = 500
    imul ecx,1920        ; Width = 1920 pixels
    add ecx,500          ; X = 500
    shl ecx,2            ; since 32-bit color = 4 bytes per pixel

    mov edi,[fb_base_addr] 
    add edi,ecx          

    mov esi, pixels
    
    movsd                 ; Copy pixel 1 (Red)
    movsd                 ; Copy pixel 2 (Green)
    movsd                 ; Copy pixel 3 (Blue)

    jmp $

SECTION .data

fb_base_addr dd 0xC00B8000 

pixels:
    dd 0x00FF0000        ; Red
    dd 0x0000FF00        ; Green
    dd 0x000000FF        ; Blue
