; setpixel - set one pixel in VGA mode 13h.  usage: setpixel <x> <y> <colour>
;   x 0..319, y 0..199, colour 0..255 (RGB332 palette index).
;   Reads the three arguments itself and passes them to the kernel in
;   registers: ebx=x, ecx=y, edx=colour, eax=47 (sys_setpixel).
[bits 32]
[org 0x00000000]

_start:
    pushad
    call .base
.base:
    pop ebp
    sub ebp,.base

    ; --- argv[1] -> x ---
    mov ebx,1
    lea edi,[ebp+abuf]
    mov eax,14                   ; sys_get_arg
    int 0x80
    cmp eax,-1
    je .usage
    lea esi,[ebp+abuf]
    call .atoi
    mov [ebp+vx],eax

    ; --- argv[2] -> y ---
    mov ebx,2
    lea edi,[ebp+abuf]
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage
    lea esi,[ebp+abuf]
    call .atoi
    mov [ebp+vy],eax

    ; --- argv[3] -> colour ---
    mov ebx,3
    lea edi,[ebp+abuf]
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage
    lea esi,[ebp+abuf]
    call .atoi
    mov [ebp+vc],eax

    ; --- hand the values to the kernel in registers ---
    mov ebx,[ebp+vx]            ; x
    mov ecx,[ebp+vy]            ; y
    mov edx,[ebp+vc]            ; colour
    mov eax,47                  ; sys_setpixel
    int 0x80
    popad
    ret

.usage:
    lea esi,[ebp+msg_usage]
    mov eax,2                   ; sys_print_cr
    int 0x80
    popad
    ret

; .atoi: esi -> decimal string; returns eax = value. Stops at NUL/CR/space.
.atoi:
    push ebx
    xor eax,eax
.lp:
    movzx ebx,byte [esi]
    test bl,bl
    jz .end
    cmp bl,13
    je .end
    cmp bl,' '
    je .end
    cmp bl,'0'
    jb .end
    cmp bl,'9'
    ja .end
    imul eax,eax,10
    sub bl,'0'
    movzx ebx,bl
    add eax,ebx
    inc esi
    jmp .lp
.end:
    pop ebx
    ret

msg_usage: db "Usage: setpixel <x> <y> <colour>",13,0

section .bss
alignb 4
vx:   resd 1
vy:   resd 1
vc:   resd 1
abuf: resb 32
