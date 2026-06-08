[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp           
    sub ebp,.get_base     
 
    lea esi,0xc0120000
    mov ecx,[esi]
    mov dword [esi],0x31415926
    mov ebx,[esi]
    cmp ebx,[esi] 
    jne .fail
    mov [esi],ecx
    lea esi,[ebp+.ok_lbl]
    mov eax,2           
    int 0x80
    ret
.fail:
    lea esi,[ebp+.fail_lbl]
    mov eax,2           
    int 0x80
    ret

.ok_lbl:
     db "systems nominal",13,0
.fail_lbl:
     db "houston, we have a problem. (0xc012000) ",13,0 
