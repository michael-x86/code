[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     

    lea esi,[ebp+help_lbl]
    mov eax,2               ; sys_print_cr
    int 0x80
    ret

help_lbl:
        db 13,"Love is like candy on a shelf ",13
        db "You want to taste and help yourself ",13
        db "The sweetest things are there for you ",13
        db "Help yourself take a few ",13
        db "That's what I want you to do ",13,13
        db 0
