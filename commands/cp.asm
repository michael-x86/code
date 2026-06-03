; cp - copy a regular file.  usage:  cp <src> <dst>
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

    ; stat src
    lea esi, [ebp + src]
    lea edi, [ebp + src_stat]
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .src_noent
    cmp dword [ebp + src_stat], 1     ; must be regular file
    jne .src_notfile

    ; create dst (fails if exists -- intentional, like cp without -f)
    lea esi, [ebp + dst]
    mov eax, 17
    int 0x80
    cmp eax, -1
    je .dst_err
    ; src_stat+4 = data pointer (kernel virt addr of content buffer)
    ; src_stat+8 = size
    lea esi, [ebp + src]
    mov ebx, [ebp + src_stat + 4]    ; data ptr from stat
    mov ecx, [ebp + src_stat + 8]    ; size
    test ecx, ecx
    jz .done                         ; empty file -- create was enough

    mov eax, 18                      ; sys_write(dst, src_data_ptr, size)
    lea esi, [ebp + dst]
    int 0x80
    cmp eax, -1
    jne .done

    lea esi, [ebp + err_write]
    mov eax, 2
    int 0x80
    ret

.src_noent:
    lea esi, [ebp + err_src_noent]
    mov eax, 2
    int 0x80
    ret
.src_notfile:
    lea esi, [ebp + err_src_notfile]
    mov eax, 2
    int 0x80
    ret
.dst_err:
    lea esi, [ebp + err_dst]
    mov eax, 2
    int 0x80
    ret
.usage:
    lea esi, [ebp + usage_cp]
    mov eax, 2
    int 0x80
.done:
    ret

err_src_noent:   db "cp: source not found", 13, 0
err_src_notfile: db "cp: source is not a regular file", 13, 0
err_dst:         db "cp: cannot create destination (exists?)", 13, 0
err_write:       db "cp: write failed", 13, 0
usage_cp:        db "usage: cp <src> <dst>", 13, 0

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
src:      times 128 db 0
dst:      times 128 db 0
src_stat: times 12  db 0
