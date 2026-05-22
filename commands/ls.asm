; ls - list entries in current working directory
[bits 32]
[org 0xC0700000]

_start:
    xor ebx, ebx                 ; index = 0
.loop:
    push ebx
    mov edi, name
    mov eax, 13                  ; sys_list_dir(ebx, edi) -> eax = type or -1
    int 0x80
    pop ebx
    cmp eax, -1
    je .done

    push ebx
    push eax                     ; save type
    mov esi, name
    mov eax, 1                   ; sys_print(esi)
    int 0x80
    pop eax
    test eax, eax                ; type 0 = dir → append '/'
    jnz .nodir
    mov ebx, '/'
    mov eax, 0                   ; sys_putchar
    int 0x80
.nodir:
    mov eax, 3                   ; sys_newline
    int 0x80
    pop ebx
    inc ebx
    jmp .loop
.done:
    ret

name: times 64 db 0
