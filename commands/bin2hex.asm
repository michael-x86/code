; bin2hex - print file contents in hex format. 
[bits 32]
[org 0x00000000]       

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base
    
    ; Get filename from argument index 1
    mov ebx,1
    mov edi,arg
    mov eax,14      ; sys_get_arg
    int 0x80
    cmp eax,-1
    je .usage

    ; Get file metadata Ptr ->  memory & size
    mov esi,arg
    mov edi,info
    mov eax,15      ; sys_stat 
    int 0x80
    cmp eax,-1
    je .nf

    ; Check if directory
    cmp dword [info],0    
    je .isdir

    ; 4. Set up the loop using the actual file pointer and size
    mov esi,[info+4]     ; esi ->  binary data in memory
    mov ecx,[info+8]     ; ecx = file size in bytes
    test ecx,ecx
    jz .empty

    mov eax,32  ; mem_dump at  esi
    int 0x80
    ret

.empty:
    lea esi,[ebp+empty_msg]
    mov eax,2                ; sys_print_cr
    int 0x80
    ret
.usage:
    lea esi,[ebp+usage_msg]
    mov eax,2               
    int 0x80
    ret
.nf:
    lea esi,[ebp+nf_msg]
    mov eax,2        
    int 0x80
    ret
.isdir:
    lea esi,[ebp+isdir_msg]
    mov eax,2               
    int 0x80
    ret

SECTION .data

usage_msg  db "usage: bin2hex <file>",13,0
nf_msg     db "bin2hex: no such file",13,0
isdir_msg  db "bin2hex: is a directory",13,0
empty_msg  db "Four petabytes confirmed ",13
           db "Upload sequence initiated ",13
           db "No intervention is required.",13,0

SECTION .bss

alignb 4
arg        resw 64 
info       resw 3 
