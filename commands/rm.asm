; rm - delete a regular file.  usage:  rm <path>
;
; ABI contract: see pwd.asm header.
[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    mov ebx, 1
    lea edi, [ebp + arg]
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; refuse to rm a directory or executable
    lea esi, [ebp + arg]
    lea edi, [ebp + stat_buf]
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .noent
    cmp dword [ebp + stat_buf], 1     ; type must be 1 (regular file)
    jne .notfile
    lea esi, [ebp + arg]
    mov eax, 19                 ; sys_unlink
    int 0x80
    cmp eax, -1
    jne .done

    lea esi, [ebp + err_rm]
    mov eax, 2
    int 0x80
    ret
.noent:
    lea esi, [ebp + err_noent]
    mov eax, 2
    int 0x80
    ret
.notfile:
    lea esi, [ebp + err_notfile]
    mov eax, 2
    int 0x80
    ret
.usage:
    lea esi, [ebp + usage_rm]
    mov eax, 2
    int 0x80
.done:
    ret

err_noent:   db "rm: no such file", 13, 0
err_notfile: db "rm: not a regular file (use rmdir for directories)", 13, 0
err_rm:      db "rm: cannot remove file", 13, 0
usage_rm:    db "usage: rm <name>", 13, 0

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
arg:       times 128 db 0
stat_buf:  times 12  db 0
