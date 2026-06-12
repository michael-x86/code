[bits 32]
[org 0x00000000]


_start:
    mov ebx, 1
    mov edi, arg
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; refuse to rm a directory or executable
    mov esi, arg
    mov edi, stat_buf
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .noent
    cmp dword [stat_buf], 1     ; type must be 1 (regular file)
    jne .notfile
    mov esi, arg
    mov eax, 19                 ; sys_unlink
    int 0x80
    cmp eax, -1
    jne .done

    mov esi, err_rm
    mov eax, 2
    int 0x80
    ret
.noent:
    mov esi, err_noent
    mov eax, 2
    int 0x80
    ret
.notfile:
    mov esi, err_notfile
    mov eax, 2
    int 0x80
    ret
.usage:
    mov esi, usage_rm
    mov eax, 2
    int 0x80
.done:
    ret

err_noent:   db "rm: no such file", 13, 0
err_notfile: db "rm: not a regular file (use rmdir for directories)", 13, 0
err_rm:      db "rm: cannot remove file", 13, 0
usage_rm:    db "usage: rm <name>", 13, 0
arg:         times 128 db 0
stat_buf:    times 12  db 0
