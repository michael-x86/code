; ls - list entries in current working directory
[bits 32]
[org 0x00000000]


_start:
    xor ebx, ebx                 ; index = 0
.loop:
    push ebx
    mov edi,name
    mov word [edi],0
    mov eax, 13                  ; sys_list_dir(ebx, edi) -> eax = type or -1
    int 0x80
    pop ebx
    cmp eax,-1
    je .done

    push ebx
    push eax                    ; save type
    mov esi,name
    mov eax,1                   ; sys_print(esi)
    int 0x80
    pop eax
    cmp eax, 2                  ; type 2 = directory
    jne .nodir
    mov ebx,'/'
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

section .bss
;----------------------------
alignb 16
name: resb 64 
