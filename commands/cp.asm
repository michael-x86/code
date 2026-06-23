[bits 32]
[org 0x00000000]

src_off       equ 0
dst_off       equ 128
src_stat_off  equ 256

_start:
    mov ebx,1
    lea edi,[ebp+src_off]
    ;mov edi,src
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage

    mov ebx,2
    lea edi,[ebp+dst_off]
    ;mov edi,dst
    mov eax,14
    int 0x80
    cmp eax,-1
    je .usage

    ; stat src
    ;mov esi,src
    ;mov edi,src_stat
    lea esi,[ebp + src_off]
    lea edi,[ebp + src_stat_off]
    mov eax,15
    int 0x80
    cmp eax,-1
    je .src_noent
    cmp dword [ebp + src_stat_off], 1
    ;cmp dword [src_stat],1     ; must be regular file
    jne .src_notfile

    ; create dst (fails if exists - intentional, like cp without -f)
    lea esi,[ebp + dst_off]
    ;mov esi,dst
    mov eax,17
    int 0x80
    cmp eax,-1
    je .dst_err
    ; src_stat+4 = data pointer 
    ; src_stat+8 = size
    lea esi, [ebp + dst_off]
    mov ebx, [ebp + src_stat_off + 4]
    mov ecx, [ebp + src_stat_off + 8]

    ;mov esi,src
    ;mov ebx,[src_stat + 4]    ; data ptr from stat
    ;mov ecx,[src_stat + 8]    ; size
    test ecx, ecx
    jz .done                   ; empty file - create was enough

    mov eax,18                 ; sys_write(dst, src_data_ptr, size)
    ;mov esi,dst
    lea esi,[ebp+dst_off]
    int 0x80
    cmp eax,-1
    jne .done

    lea esi, [ebp + err_write]
    ;mov esi,err_write
    mov eax,2
    int 0x80
    mov eax,0
    int 0x80

.src_noent:
    ;mov esi, err_src_noent
    lea esi, [ebp + err_src_noent]
    mov eax, 2
    int 0x80
    ret
.src_notfile:
    ;mov esi, err_src_notfile
    lea esi, [ebp + err_src_notfile]
    mov eax, 2
    int 0x80
    ret
.dst_err:
    lea esi, [ebp + err_dst]
    ;mov esi, err_dst
    mov eax, 2
    int 0x80
    ret
.usage:
    ;mov esi, usage_cp
    lea esi, [ebp + usage_cp]
    mov eax, 2
    int 0x80
    ret
.done:
    ret

err_src_noent:  db "cp: source not found", 13, 0
err_src_notfile:db "cp: source is not a regular file", 13, 0
err_dst:        db "cp: cannot create destination (exists?)", 13, 0
err_write:      db "cp: write failed", 13, 0
usage_cp:       db "usage: cp <src> <dst>", 13, 0
