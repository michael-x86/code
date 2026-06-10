; =============================================================================
; vfs.inc — VFS path layer (current working directory, path resolution)
; =============================================================================
;
; fs_core.inc gives us "block N", "inode N", "lookup name in dir". This
; module adds the path layer: cwd management, absolute/relative/. /..
; resolution, and a few small helpers the rest of the kernel uses to
; convert between path strings and inode numbers.
;
; The cwd is stored in cwd_buf (see bss.inc). The shell, the chdir
; syscall, and exec_bin all read it; nothing else writes it.
;
; Path semantics
; --------------
;   /absolute      → starts with '/', resolved from inode 0 (root)
;   relative       → resolved against cwd by string concatenation
;   .              → cwd itself (used by tab completion a lot)
;   ..             → parent of cwd (truncated to '/' at the root)
;
; Multi-component relative paths ("foo/bar") are handled by string-
; concatenating cwd + "/" + path, then walking the result with
; fs_lookup_inode. Single-component paths are also walked, but
; basename_if_child is the fast path that lets the chdir/list_dir
; paths skip a copy when the path is a direct child of cwd.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; ensure_fs_ready — lightweight FS bring-up for command execution
;
; Sets fs_ready=1 once the superblock has been loaded with a valid
;-- magic. Does NOT load the bitmap — that is the more expensive step
;-- that only matters for write paths, and is done by load_fs_persist
;-- on the first shell entry. Splitting these lets the very first shell
;-- command land a few ms sooner.
; -----------------------------------------------------------------------------
ensure_fs_ready:
    pushad
    cmp byte [fs_ready], 1
    je .ok

    mov eax, [0x500]
    mov [fs_ata_base], eax
    mov al, [0x504]
    mov [fs_ata_drive], al
    mov eax, [0x508]
    mov [fs_base_lba], eax

    ; If port base is 0, bootloader handoff wasn't written (e.g. in
    ;-- webulator without a bootloader).  Use safe defaults so that
    ;-- filesystem code doesn't operate on zeroed ATA registers.
    cmp dword [fs_ata_base], 0
    jne .have_handoff
    mov dword [fs_ata_base], 0x1F0
    mov byte  [fs_ata_drive], 0xF0
    mov dword [fs_base_lba], 0
.have_handoff:

    call load_superblock
    cmp dword [sb_buf + SB_MAGIC], FS_MAGIC
    jne .ok
    mov byte [fs_ready], 1

.ok:
    popad
    ret


; -----------------------------------------------------------------------------
; fs_resolve — turn a (possibly relative) path into an absolute path
;   in:  esi = path, edi = dst buffer (at least CWD_BUF_SIZE bytes)
;   out: dst is filled with the absolute path (NUL-terminated)
;
;   . and .. are resolved by string manipulation here, before
;-- fs_lookup_inode walks the result. Absolute paths are copied through
;-- verbatim; relative paths get cwd prepended with a separator.
; -----------------------------------------------------------------------------
fs_resolve:
    push eax
    push ebx
    push esi
    push edi

    cmp byte [esi], '/'
    je .abs
    cmp byte [esi], '.'
    jne .relative
    cmp byte [esi + 1], 0
    je .dot
    cmp byte [esi + 1], '.'
    jne .relative
    cmp byte [esi + 2], 0
    jne .relative
    ; fall through to .dotdot
.dotdot:
    mov ebx, edi                         ; remember start of dst
    mov esi, cwd_buf
.cp_dd:
    lodsb
    stosb
    test al, al
    jnz .cp_dd
    dec edi                              ; step back over the trailing NUL
.find:
    dec edi
    cmp edi, ebx
    jbe .at_root
    cmp byte [edi], '/'
    jne .find
    cmp edi, ebx
    je .at_root
    mov byte [edi], 0                    ; truncate at the slash
    jmp .done
.at_root:
    mov edi, ebx
    mov byte [edi], '/'
    mov byte [edi + 1], 0
    jmp .done
.dot:
    mov esi, cwd_buf
.abs:
.cp_abs:
    lodsb
    stosb
    test al, al
    jnz .cp_abs
    jmp .done
.relative:
    push edi                             ; save dst start for root reset
    mov ebx, esi                         ; remember original path
    mov esi, cwd_buf
    xor edx, edx                         ; dst byte counter (bounds check)
.cp_cwd:
    lodsb
    stosb
    inc edx
    cmp edx, CWD_BUF_SIZE - 2
    jae .rel_done                        ; overflow guard
    test al, al
    jnz .cp_cwd
    dec edi                              ; back over trailing NUL
    dec edx

.rel_loop:
    mov esi, ebx
.rel_skip_slash:
    cmp byte [esi], '/'
    jne .rel_check
    inc esi
    jmp .rel_skip_slash
.rel_check:
    cmp byte [esi], 0
    je .rel_done
    ; Check for ".."
    cmp byte [esi], '.'
    jne .rel_copy
    cmp byte [esi + 1], '.'
    jne .rel_dot
    mov al, [esi + 2]
    cmp al, '/'
    je .rel_dotdot
    cmp al, 0
    je .rel_dotdot
    jmp .rel_copy
.rel_dot:
    ; Single "." — skip the component entirely
    mov al, [esi + 1]
    cmp al, '/'
    je .rel_skip_dot
    cmp al, 0
    je .rel_done
    jmp .rel_copy
.rel_skip_dot:
    add esi, 2
    mov ebx, esi
    jmp .rel_loop

.rel_dotdot:
    add esi, 2
    cmp byte [esi], '/'
    jne .rel_strip
    inc esi
.rel_strip:
    mov ebx, esi
    cmp edx, 1
    jbe .rel_strip_root
    dec edi
    dec edx
    cmp byte [edi], '/'
    jne .rel_strip
    cmp edx, 1
    jbe .rel_strip_root
    mov byte [edi], 0
    jmp .rel_loop
.rel_strip_root:
    mov edi, [esp]                       ; reset to start of dst
    mov byte [edi], '/'
    mov byte [edi + 1], 0
    mov edx, 1
    inc edi
    jmp .rel_loop

.rel_copy:
    cmp edx, 1
    jbe .rel_copy_path
    cmp byte [edi - 1], '/'
    je .rel_copy_path
    cmp edx, CWD_BUF_SIZE - 2
    jae .rel_done
    mov byte [edi], '/'
    inc edi
    inc edx
.rel_copy_path:
    cmp edx, CWD_BUF_SIZE - 1
    jae .rel_done
    lodsb
    test al, al
    jz .rel_done
    cmp al, '/'
    je .rel_comp_done
    stosb
    inc edx
    jmp .rel_copy_path
.rel_comp_done:
    mov ebx, esi
    mov byte [edi], 0
    jmp .rel_loop

.rel_done:
    mov byte [edi], 0
    pop edi                              ; discard saved start
    jmp .done
.done:
    pop edi
    pop esi
    pop ebx
    pop eax
    ret


; -----------------------------------------------------------------------------
; fs_lookup_inode — walk an absolute path, returning the inode number
;   in:  esi = absolute path (NUL-terminated, must start with '/')
;   out: eax = inode number, -1 if any component is not found
;
; We don't go through dir_lookup on every component — instead we copy
; each path component into path_buf and do a single dir_lookup call
; per component. The root inode is 0 by convention.
; -----------------------------------------------------------------------------
fs_lookup_inode:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov ebx, 0                           ; current inode = root
    cmp byte [esi], '/'
    jne .not_found
    cmp byte [esi + 1], 0
    je .root_found                       ; path is exactly "/"
    inc esi
.component:
    mov edi, path_buf
.cp_comp:
    mov al, [esi]
    test al, al
    jz .last_comp
    cmp al, '/'
    je .got_comp
    stosb
    inc esi
    jmp .cp_comp
.got_comp:
    mov byte [edi], 0
    inc esi
    push esi
    mov esi, path_buf
    call dir_lookup
    pop esi
    cmp eax, -1
    je .not_found
    mov ebx, eax
    jmp .component
.last_comp:
    mov byte [edi], 0
    cmp byte [path_buf], 0
    je .root_found
    push esi
    mov esi, path_buf
    call dir_lookup
    pop esi
    cmp eax, -1
    je .not_found
    mov ebx, eax
.root_found:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.not_found:
    mov eax, -1
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
