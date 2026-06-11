[bits 32]
[org 0x00000000]            ; Base is relative to whatever exec_bin allocates

_start:
    call .get_base
.get_base:
    pop ebp          ; ebp = absolute runtime address of .get_base
    sub ebp, .get_base      

    ; --- Fetch global argument 1 ---
    mov ebx,1
    lea edi,[ebp+arg]    ; Calculate absolute runtime address of arg buffer
    mov eax,14           ; sys_get_arg
    int 0x80
    cmp eax,-1
    je .usage

    ; --- Create Directory ---
    lea esi,[ebp + arg]
    mov eax,20             ; sys_mkdir
    int 0x80
    cmp eax,-1
    je .err
    ret                   

.usage:
    lea esi,[ebp + usage_msg]
    mov eax,2              ; sys_print
    int 0x80
    ret

.err:
    lea esi,[ebp + err_msg]
    mov eax,2              ; sys_print
    int 0x80
    ret

; --- Data Section ---
usage_msg db "usage: mkdir <path>", 13, 0
err_msg   db "mkdir: cannot create directory (exists or FS full)", 13, 0

; --- Buffer Section ---
arg:      times 128 db 0
