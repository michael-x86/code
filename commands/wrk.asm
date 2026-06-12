[bits 32]
[org 0x00000000]       

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base
    
    ; Get filename 
    mov ebx,0
    mov edi,arg
    mov eax,14      ; sys_get_arg
    int 0x80
    cmp eax,-1
    je .fail

    ; metadata -> memory & size
    mov esi,arg
    mov edi,info
    mov eax,15      ; sys_stat 
    int 0x80
    cmp eax,-1
    je .fail

    ; directory
    cmp dword [info],0    
    je .fail

    mov edx,3141         ; seed
    mov edi,3141            
    mov esi,[info+4]     ; esi ->  binary data in memory
    mov ecx,[info+8]     ; ecx =   size in bytes
    test ecx,ecx
    jz .empty
    xor edi,[esi]
    mov eax,23   ; stack & regs
    int 0x80
    mov eax,32   ; esi -> mem_dump
    int 0x80
    ret

.fail:
.empty:
    ret

SECTION .bss

alignb 4
arg        resw 64 
info       resw 16 
