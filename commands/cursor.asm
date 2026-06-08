[bits 32]
[org 0x00000000]

GLOBAL _start

; Inputs:  ecx = X coordinate (0-79)
;          edx = Y coordinate (0-24)

_start:
    push eax
    push ebx
    push edx

    mov ecx,20
    mov edx,20

    ; ebx = (Y*80)+X
    mov ebx,edx
    imul ebx,80
    add ebx,ecx

    mov al,0x0E          ; 0x0E is the command for Cursor Location High
    mov dx,0x3D4         ; VGA CRT Controller Index Port
    out dx,al

    mov al,bh            ; Get the high byte of our 16-bit position (from ebx)
    mov dx,0x3D5         ; VGA CRT Controller Data Port
    out dx,al

    ; ---- Send the LOW byte ----
    mov al, 0x0F          ; 0x0F is the command for Cursor Location Low
    mov dx, 0x3D4         ; VGA CRT Controller Index Port
    out dx, al

    mov al, bl            ; Get the low byte of our 16-bit position (from ebx)
    mov dx, 0x3D5         ; VGA CRT Controller Data Port
    out dx, al

    pop edx
    pop ebx
    pop eax
    ret
