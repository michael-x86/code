[bits 32]
[org 0x00000000]

_start:
    mov ebx,1
    mov edi,src
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage

    mov ebx,2
    mov edi,dst
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage

    ; src must be a regular file
    mov esi,src
    mov edi,src_stat
    mov eax,15
    int 0x80
    cmp eax,-1
    je .noent
    cmp dword [src_stat],1
    jne .notfile

    ; create dst
    mov esi,dst
    mov eax,17
    int 0x80
    cmp eax,-1
    je .dst_err

    ; write src content to dst
    mov esi,dst
    mov ebx,[src_stat+4]    ; data ptr
    mov ecx,[src_stat+8]    ; size
    test ecx,ecx
    jz .remove                  ; empty file â€” skip write
    mov eax,18
    int 0x80
    cmp eax,-1
    je .write_err

.remove:
    ; unlink src
    mov esi,src
    mov eax,19
    int 0x80
    cmp eax,-1
    jne .done
    mov esi,err_rm
    mov eax,2
    int 0x80
    ret

.noent:
    mov esi,err_noent
    mov eax,2
    int 0x80
    ret
.notfile:
    mov esi,err_notfile
    mov eax,2
    int 0x80
    ret
.dst_err:
    mov esi,err_dst
    mov eax,2
    int 0x80
    ret
.write_err:
    ; clean up dst to avoid partial file
    mov esi,dst
    mov eax,19
    int 0x80
    mov esi,err_write
    mov eax,2
    int 0x80
    ret
.usage:
    mov esi,usage_mv
    mov eax,2
    int 0x80
.done:
    ret

err_noent:  db "mv: source not found", 13, 0
err_notfile:db "mv: source is not a regular file", 13, 0
err_dst:    db "mv: cannot create destination (exists?)", 13, 0
err_write:  db "mv: write failed", 13, 0
err_rm:     db "mv: could not remove source after copy", 13, 0
usage_mv:   db "usage: mv <src> <dst>", 13, 0
src:        times 128 db 0
dst:        times 128 db 0
src_stat:   times 12  db 0
