[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp                
    sub ebp, .get_base      ; ebp = delta/base load address

    ; --- Fetch global argument 1 ---
    mov ebx,1
    lea edi,[ebp+arg]
    mov eax,14          
    int 0x80
    cmp eax,-1
    je .usage

    ; --- Stat the file/directory ---
    lea esi,[ebp+arg]
    lea edi,[ebp+stat_buf]
    mov eax,15             ; sys_stat
    int 0x80
    cmp eax,-1
    je .noent

    ; --- Match Kernel Definition: Directory type MUST be 0 ---
    lea edx,[ebp+stat_buf]
    mov eax,[edx]              ; Fetch type dword from stat_buf offset 0
    cmp eax,0                  ; In your kernel, sys_mkdir sets this to 0!
    jne .notdir

    ; --- Remove Directory ---
    lea esi,[ebp+arg]
    mov eax,21             ; sys_rmdir
    int 0x80
    cmp eax,-1
    jne .done

    lea esi,[ebp+err_rmdir]
    mov eax,2              ; sys_print
    int 0x80
    ret

.noent:
    lea esi,[ebp+err_noent]
    mov eax,2
    int 0x80
    ret

.notdir:
    lea esi,[ebp+err_notdir]
    mov eax,2
    int 0x80
    ret

.usage:
    lea esi,[ebp + usage_rmdir]
    mov eax,2
    int 0x80
.done:
    ret

; --- Data Section ---
err_noent:   db "rmdir: no such directory", 13, 0
err_notdir:  db "rmdir: not a directory (use rm for regular files)", 13, 0
err_rmdir:   db "rmdir: directory not empty or permission denied", 13, 0
usage_rmdir: db "usage: rmdir <path>", 13, 0

; --- Buffer Section ---
    align 4
arg:         times 128 db 0
stat_buf:    times 12  db 0
