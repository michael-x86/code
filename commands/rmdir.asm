[bits 32]
[org 0xC0700000]

_start:
    mov ebx, 1
    mov edi, arg
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; stat must exist and be type 0 (dir)
    mov esi, arg
    mov edi,stat_buf
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .noent
    cmp dword [stat_buf], 0     ; type == dir?
    jne .notdir

    ; check it is empty (sys_list_dir index 0 from that path)
    ; cheapest: chdir into it, list index 0, chdir back
    mov esi, arg
    mov eax, 12                 ; sys_chdir
    int 0x80
    cmp eax, -1
    je .noent

    xor ebx, ebx
    mov edi, tmp
    mov eax, 13                 ; sys_list_dir(0, tmp) -> -1 if empty
    int 0x80
    cmp eax, -1
    je .isempty

    ; not empty â€” go back up
    mov esi, dotdot
    mov eax, 12
    int 0x80
    mov esi, err_notempty
    mov eax, 2
    int 0x80
    ret

.isempty:
    mov esi, dotdot
    mov eax, 12                 ; cd ..
    int 0x80

    mov esi, arg
    mov eax, 21                 ; sys_rmdir(esi) -> 0/-1
    int 0x80
    cmp eax, -1
    jne .done
    mov esi, err_rmdir
    mov eax, 2
    int 0x80
    ret

.noent:
    mov esi, err_noent
    mov eax, 2
    int 0x80
    ret
.notdir:
    mov esi, err_notdir
    mov eax, 2
    int 0x80
    ret
.usage:
    mov esi, usage_rmdir
    mov eax, 2
    int 0x80
.done:
    ret

dotdot:      db "..", 0
err_noent:   db "rmdir: no such directory", 13, 0
err_notdir:  db "rmdir: not a directory", 13, 0
err_notempty:db "rmdir: directory not empty", 13, 0
err_rmdir:   db "rmdir: cannot remove directory", 13, 0
usage_rmdir: db "usage: rmdir <name>", 13, 0
arg:         times 128 db 0
stat_buf:    times 12  db 0
tmp:         times 128 db 0
