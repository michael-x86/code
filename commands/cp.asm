[bits 32]
[org 0x00000000]

_start:
    mov ebx, 1
    mov edi, src
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    mov ebx, 2
    mov edi, dst
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; stat src
    mov esi, src
    mov edi, src_stat
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .src_noent
    cmp dword [src_stat], 1     ; must be regular file
    jne .src_notfile

    ; create dst (fails if exists â€” intentional, like cp without -f)
    mov esi, dst
    mov eax, 17
    int 0x80
    cmp eax, -1
    je .dst_err
    ; src_stat+4 = data pointer (kernel virt addr of content buffer)
    ; src_stat+8 = size
    mov esi, src
    mov ebx, [src_stat + 4]    ; data ptr from stat
    mov ecx, [src_stat + 8]    ; size
    test ecx, ecx
    jz .done                    ; empty file â€” create was enough

    mov eax, 18                 ; sys_write(dst, src_data_ptr, size)
    mov esi, dst
    int 0x80
    cmp eax, -1
    jne .done

    mov esi, err_write
    mov eax, 2
    int 0x80
    ret

.src_noent:
    mov esi, err_src_noent
    mov eax, 2
    int 0x80
    ret
.src_notfile:
    mov esi, err_src_notfile
    mov eax, 2
    int 0x80
    ret
.dst_err:
    mov esi, err_dst
    mov eax, 2
    int 0x80
    ret
.usage:
    mov esi, usage_cp
    mov eax, 2
    int 0x80
.done:
    ret

err_src_noent:  db "cp: source not found", 13, 0
err_src_notfile:db "cp: source is not a regular file", 13, 0
err_dst:        db "cp: cannot create destination (exists?)", 13, 0
err_write:      db "cp: write failed", 13, 0
usage_cp:       db "usage: cp <src> <dst>", 13, 0
src:            times 128 db 0
dst:            times 128 db 0
src_stat:       times 12  db 0
