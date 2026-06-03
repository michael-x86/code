; mv - rename a file by copy + unlink.  usage:  mv <src> <dst>
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
    lea edi, [ebp + src]
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    mov ebx, 2
    lea edi, [ebp + dst]
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; src must be a regular file
    lea esi, [ebp + src]
    lea edi, [ebp + src_stat]
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .noent
    cmp dword [ebp + src_stat], 1
    jne .notfile

    ; create dst
    lea esi, [ebp + dst]
    mov eax, 17
    int 0x80
    cmp eax, -1
    je .dst_err

    ; write src content to dst
    lea esi, [ebp + dst]
    mov ebx, [ebp + src_stat + 4]    ; data ptr
    mov ecx, [ebp + src_stat + 8]    ; size
    test ecx, ecx
    jz .remove                       ; empty file -- skip write
    mov eax, 18
    int 0x80
    cmp eax, -1
    je .write_err

.remove:
    ; unlink src
    lea esi, [ebp + src]
    mov eax, 19
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
.dst_err:
    lea esi, [ebp + err_dst]
    mov eax, 2
    int 0x80
    ret
.write_err:
    ; clean up dst to avoid partial file
    lea esi, [ebp + dst]
    mov eax, 19
    int 0x80
    lea esi, [ebp + err_write]
    mov eax, 2
    int 0x80
    ret
.usage:
    lea esi, [ebp + usage_mv]
    mov eax, 2
    int 0x80
.done:
    ret

err_noent:   db "mv: source not found", 13, 0
err_notfile: db "mv: source is not a regular file", 13, 0
err_dst:     db "mv: cannot create destination (exists?)", 13, 0
err_write:   db "mv: write failed", 13, 0
err_rm:      db "mv: could not remove source after copy", 13, 0
usage_mv:    db "usage: mv <src> <dst>", 13, 0

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
src:      times 128 db 0
dst:      times 128 db 0
src_stat: times 12  db 0
