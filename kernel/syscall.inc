; =============================================================================
; syscall.inc — int 0x80 dispatcher and all syscall handlers
; =============================================================================
;
; Calling convention:
;   eax = syscall number
;   ebx, ecx, edx, esi, edi = arguments
;   eax = return value (-1 on bad syscall number)
;   All other GPRs preserved.
; =============================================================================

set_syscall:
    mov eax, syscall_isr
    mov edi, idt_start + 0x80 * 8
    mov word [edi], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], IDT_DPL3
    shr eax, 16
    mov word [edi + 6], ax
    ret

syscall_isr:
    cmp eax, SYSCALL_COUNT
    jae .bad
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push ebp
    call [syscall_table + eax * 4]
    pop ebp
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    iretd
.bad:
    mov eax, -1
    iretd

; ── Individual syscall handlers ─────────────────────────────────────────────

sys_putchar:                             ; 0: ebx = char
    mov eax, ebx
    call putchar
    xor eax, eax
    ret

sys_print:                               ; 1: esi = string
    call print
    xor eax, eax
    ret

sys_print_cr:                            ; 2: esi = string
    call print_cr
    xor eax, eax
    ret

sys_newline:                             ; 3
    call newline
    xor eax, eax
    ret

sys_cls:                                 ; 4
    call cls
    xor eax, eax
    ret

sys_print_hex:                           ; 5: ebx = value
    mov eax, ebx
    call print_hex_dword
    xor eax, eax
    ret

sys_print_int:                           ; 6: ebx = value
    mov eax, ebx
    call print_int_decimal
    xor eax, eax
    ret

sys_get_key:                             ; 7: -> eax = ASCII (0 if empty)
    call get_key
    movzx eax, al
    ret

sys_get_tick:                            ; 8: -> eax = tick_count
    mov eax, [tick_count]
    ret

sys_shutdown:                            ; 9: does not return
    call shutdown
    ret

sys_read_mem:                            ; 10: ebx = addr -> eax = [ebx]
    mov eax, [ebx]
    ret

sys_getcwd:                              ; 11: edi = dst buffer
    push esi
    mov esi, cwd_buf
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    pop esi
    xor eax, eax
    ret

; ── VFS syscalls (block-based filesystem) ──────────────────────────────────

sys_chdir:                               ; 12: esi = path -> eax = 0/-1
    push esi
    push edi
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .nf
    mov ebx, eax
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    jne .nf
    mov esi, resolve_buf
    mov edi, cwd_buf
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    pop edi
    pop esi
    xor eax, eax
    ret
.nf:
    pop edi
    pop esi
    mov eax, -1
    ret

; -----------------------------------------------------------------------------
; sys_list_dir — list directory entries by index
;   in:  ebx = index, edi = dst buffer
;   out: eax = file type (1=file 2=dir), or -1 if not found
; -----------------------------------------------------------------------------
sys_list_dir:                            ; 13: ebx = index, edi = dst -> type/-1
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    mov esi, cwd_buf
    call fs_lookup_inode
    cmp eax, -1
    je .ld_nf
    mov ebp, eax             ; ebp = dir inode
    mov ecx, ebx             ; ecx = target index
    xor ebx, ebx             ; ebx = block index
.ld_blk:
    cmp ebx, 12
    jae .ld_nf
    push ecx
    push ebx
    mov ecx, ebp
    call load_inode
    pop ebx
    pop ecx
    mov eax, [inode_buf + INOFS_DIRECT + ebx * 4]
    test eax, eax
    jz .ld_nf
    push ebx
    push ecx
    push ebp
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ebp
    pop ecx
    pop ebx
    mov edi, dir_block_buf
    mov edx, BLOCK_SIZE
.ld_ent:
    cmp edx, 8
    jb .ld_next_blk
    movzx eax, word [edi + 4]
    test eax, eax
    jz .ld_next_blk
    push eax
    mov esi, [edi]           ; inode number
    pop eax
    test esi, esi
    jz .ld_skip
    test ecx, ecx
    jz .ld_found
    dec ecx
.ld_skip:
    sub edx, eax
    add edi, eax
    jmp .ld_ent
.ld_found:
    ; Copy name to dst (original edi is at [esp+12])
    mov eax, [esp + 12]
    push esi
    movzx ecx, byte [edi + 6] ; name_len
    lea esi, [edi + 8]
    push edi
    mov edi, eax
    cld
    rep movsb
    mov byte [edi], 0
    pop edi
    pop esi
    ; Get file type
    movzx eax, byte [edi + 7]
    test al, al
    jnz .ld_type_ok
    mov ebx, esi
    push eax
    call load_inode
    pop eax
    mov eax, [inode_buf + INOFS_TYPE]
.ld_type_ok:
    add esp, 20
    pop ebx
    ret
.ld_next_blk:
    inc ebx
    jmp .ld_blk
.ld_nf:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov eax, -1
    ret

sys_get_arg:                             ; 14: ebx = index, edi = dst -> 0/-1
    cmp ebx, [argc]
    jae .nf
    push esi
    mov esi, [argv + ebx * 4]
    test esi, esi
    jz .nf_pop
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    pop esi
    xor eax, eax
    ret
.nf_pop:
    pop esi
.nf:
    mov eax, -1
    ret

sys_stat:                                ; 15: esi = path, edi = info(12B) -> 0/-1
    push esi
    push edi
    push ebx
    mov ebx, edi
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .nf
    mov edx, eax
    push edx
    mov ebx, eax
    call load_inode
    pop edx
    mov eax, [inode_buf + INOFS_TYPE]
    mov [ebx], eax
    mov eax, [inode_buf + INOFS_SIZE]
    mov [ebx + 4], eax
    mov dword [ebx + 8], 0
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.nf:
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_print_n:                             ; 16: esi = ptr, ecx = count -> 0
.lp:
    test ecx, ecx
    jz .done
    mov al, [esi]
    inc esi
    dec ecx
    cmp al, 0x0A
    je .nl
    cmp al, 0x0D
    je .lp
    cmp al, 0x09
    jne .raw
    mov al, ' '
.raw:
    push ecx
    push esi
    call putchar
    pop esi
    pop ecx
    jmp .lp
.nl:
    push ecx
    push esi
    call newline
    pop esi
    pop ecx
    jmp .lp
.done:
    xor eax, eax
    ret

; -----------------------------------------------------------------------------
; sys_create — create a new file
;   in:  esi = path -> eax = 0/-1
; -----------------------------------------------------------------------------
sys_create:                              ; 17: esi = path -> 0/-1
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    jne .sc_err
    ; Find last '/' to split parent/name
    mov esi, resolve_buf
    xor ecx, ecx             ; last slash pos
    xor edx, edx             ; counter
.sc_scan:
    lodsb
    test al, al
    jz .sc_got
    cmp al, '/'
    jne .sc_next
    mov ecx, edx
.sc_next:
    inc edx
    jmp .sc_scan
.sc_got:
    ; Copy parent path to path_buf
    mov esi, resolve_buf
    mov edi, path_buf
    test ecx, ecx
    jz .sc_parent_root
    push ecx
    rep movsb
    pop ecx
    mov byte [edi], 0
    jmp .sc_parent_done
.sc_parent_root:
    mov byte [edi], '/'
    mov byte [edi + 1], 0
.sc_parent_done:
    ; Copy name to temp area
    lea esi, [resolve_buf + ecx + 1]
    mov edi, file_block_buf + 256
    mov [name_ptr], edi       ; save name pointer
    push ecx
    xor ecx, ecx
.sc_cname:
    lodsb
    test al, al
    jz .sc_cname_done
    stosb
    inc ecx
    jmp .sc_cname
.sc_cname_done:
    mov byte [edi], 0
    pop ecx
    mov [file_bytes_left], ecx ; save name_len
    ; Look up parent inode
    mov esi, path_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sc_err
    mov ebx, eax
    ; Allocate new inode
    call ialloc
    cmp eax, -1
    je .sc_err
    mov edx, eax             ; edx = new inode num
    ; Initialize inode
    mov dword [inode_buf + INOFS_TYPE], INODE_FILE
    mov dword [inode_buf + INOFS_SIZE], 0
    push edi
    lea edi, [inode_buf + INOFS_DIRECT]
    mov ecx, 14               ; 12 direct + sind + dind
    xor eax, eax
    cld
    rep stosd
    pop edi
    mov ebx, edx
    call write_inode
    ; Add directory entry
    mov esi, [name_ptr]
    mov ecx, INODE_FILE
    call dir_add_entry
    cmp eax, -1
    je .sc_err
    popad
    xor eax, eax
    ret
.sc_err:
    popad
    mov eax, -1
    ret

; -----------------------------------------------------------------------------
; sys_write — write data to a file
;   in:  esi = path, ebx = buf, ecx = n -> bytes written / -1
; -----------------------------------------------------------------------------
sys_write:                               ; 18: esi = path, ebx = buf, ecx = n
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sw_err
    mov ebx, eax
    mov esi, [esp + 4]       ; original ebx = source buffer
    mov ecx, [esp]           ; original ecx = count
    xor edx, edx             ; offset = 0
    call file_write
    cmp eax, -1
    je .sw_err
    popad
    xor eax, eax
    ret
.sw_err:
    popad
    mov eax, -1
    ret

; -----------------------------------------------------------------------------
; sys_unlink — delete a file
;   in:  esi = path -> eax = 0/-1
; -----------------------------------------------------------------------------
sys_unlink:                              ; 19: esi = path -> 0/-1
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .su_err
    mov ebx, eax
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FILE
    jne .su_err
    call ifree
    popad
    xor eax, eax
    ret
.su_err:
    popad
    mov eax, -1
    ret

; -----------------------------------------------------------------------------
; sys_mkdir — create a directory
;   in:  esi = path -> eax = 0/-1
; -----------------------------------------------------------------------------
sys_mkdir:                               ; 20: esi = path -> 0/-1
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    jne .sm_err
    ; Find last '/'
    mov esi, resolve_buf
    xor ecx, ecx
    xor edx, edx
.sm_scan:
    lodsb
    test al, al
    jz .sm_got
    cmp al, '/'
    jne .sm_next
    mov ecx, edx
.sm_next:
    inc edx
    jmp .sm_scan
.sm_got:
    mov esi, resolve_buf
    mov edi, path_buf
    test ecx, ecx
    jz .sm_parent_root
    push ecx
    rep movsb
    pop ecx
    mov byte [edi], 0
    jmp .sm_parent_done
.sm_parent_root:
    mov byte [edi], '/'
    mov byte [edi + 1], 0
.sm_parent_done:
    lea esi, [resolve_buf + ecx + 1]
    mov edi, file_block_buf + 256
    mov [name_ptr], edi
    push ecx
    xor ecx, ecx
.sm_cname:
    lodsb
    test al, al
    jz .sm_cname_done
    stosb
    inc ecx
    jmp .sm_cname
.sm_cname_done:
    mov byte [edi], 0
    pop ecx
    mov [file_bytes_left], ecx
    mov esi, path_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sm_err
    mov ebx, eax
    call ialloc
    cmp eax, -1
    je .sm_err
    mov edx, eax
    mov dword [inode_buf + INOFS_TYPE], INODE_DIR
    mov dword [inode_buf + INOFS_SIZE], 0
    push edi
    lea edi, [inode_buf + INOFS_DIRECT]
    mov ecx, 14
    xor eax, eax
    cld
    rep stosd
    pop edi
    mov ebx, edx
    call write_inode
    mov esi, [name_ptr]
    mov ecx, INODE_DIR
    call dir_add_entry
    cmp eax, -1
    je .sm_err
    popad
    xor eax, eax
    ret
.sm_err:
    popad
    mov eax, -1
    ret

; -----------------------------------------------------------------------------
; sys_rmdir — remove an empty directory
;   in:  esi = path -> eax = 0/-1
; -----------------------------------------------------------------------------
sys_rmdir:                               ; 21: esi = path -> 0/-1
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sr_err
    mov ebx, eax
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    jne .sr_err
    ; Check empty: all direct block pointers must be zero
    push ebx
    lea esi, [inode_buf + INOFS_DIRECT]
    mov ecx, 12
.sr_chk:
    lodsd
    test eax, eax
    jnz .sr_not_empty
    dec ecx
    jnz .sr_chk
    pop ebx
    call ifree
    popad
    xor eax, eax
    ret
.sr_not_empty:
    pop ebx
.sr_err:
    popad
    mov eax, -1
    ret

; ── Memory management syscalls ─────────────────────────────────────────────

sys_get_ps_info:                         ; 22: ebx = ptr to 16-byte buffer -> 0/-1
    cmp ebx, 0
    je .error
    push edx
    mov edx, [current_task]
    mov [ebx + 0], edx
    mov edx, [task0_esp]
    mov [ebx + 4], edx
    mov edx, [task1_esp]
    mov [ebx + 8], edx
    mov edx, [task2_esp]
    mov [ebx + 12], edx
    pop edx
    xor eax, eax
    ret
.error:
    mov eax, -1
    ret

sys_alloc_pages:                         ; 23: ecx = page count -> eax = vaddr/-1
    push ebx
    push ecx
    push edx
    push edi
    push esi
    mov eax, ecx
    test eax, eax
    jz .err
    add eax, 4095
    shr eax, 12
    mov ecx, eax
    push ecx
    call find_free_virt
    pop ecx
    jc .err_pop
    mov edi, eax
    mov edx, eax
.alloc_loop:
    test ecx, ecx
    jz .done_alloc
    push edx
    push ecx
    call alloc_page
    pop ecx
    pop edx
    jc .err_free
    mov ebx, eax
    mov eax, edx
    push edx
    push ecx
    mov ecx, PAGE_PRESENT_RW
    call map_page
    pop ecx
    pop edx
    jc .err_free
    add edx, PAGE_SIZE
    dec ecx
    jmp .alloc_loop
.done_alloc:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    call register_allocation
    pop esi
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret
.err_free:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    test ebx, ebx
    jz .err_pop
    call free_pages
.err_pop:
    pop ecx
.err:
    pop esi
    pop edi
    pop edx
    pop ecx
    pop ebx
    mov eax, -1
    ret

sys_free_pages:                          ; 24: ebx = vaddr -> 0/-1
    push ebx
    push ecx
    push esi
    mov esi, alloc_table
    mov ecx, alloc_table_count
.search:
    cmp [esi], ebx
    je .found
    add esi, 8
    loop .search
    pop esi
    pop ecx
    pop ebx
    mov eax, -1
    ret
.found:
    mov ebx, [esi + 4]
    push eax
    call free_pages
    pop eax
    mov dword [esi], 0
    mov dword [esi + 4], 0
    pop esi
    pop ecx
    pop ebx
    xor eax, eax
    ret

sys_write_mem:                           ; 25: ebx = addr, ecx = byte value -> 0/-1
    mov byte [ebx], cl
    xor eax, eax
    ret

sys_hex2int:                             ; 26: esi = string -> eax = value
    push ebx
    push edx
    xor eax, eax
    xor ebx, ebx
    mov bl, [esi]
    cmp bl, '0'
    jne .parse
    cmp byte [esi + 1], 'x'
    je .skip_prefix
    cmp byte [esi + 1], 'X'
    jne .parse
.skip_prefix:
    add esi, 2
.parse:
    movzx ebx, byte [esi]
    test bl, bl
    jz .done
    cmp bl, '0'
    jb .done
    cmp bl, '9'
    jle .digit
    cmp bl, 'A'
    jb .done
    cmp bl, 'F'
    jle .upper
    cmp bl, 'a'
    jb .done
    cmp bl, 'f'
    ja .done
.lower:
    sub bl, 'a' - 10
    jmp .nib
.upper:
    sub bl, 'A' - 10
    jmp .nib
.digit:
    sub bl, '0'
.nib:
    shl eax, 4
    or eax, ebx
    inc esi
    jmp .parse
.done:
    pop edx
    pop ebx
    ret

sys_asc2int:                             ; 27: esi = string -> eax = value
    push ebx
    push edx
    xor eax, eax
    mov ebx, 10
.loop:
    movzx edx, byte [esi]
    test dl, dl
    jz .done
    cmp dl, ' '
    je .done
    sub edx, '0'
    imul eax, eax, 10
    add eax, edx
    inc esi
    jmp .loop
.done:
    pop edx
    pop ebx
    ret

sys_get_proc_info:                       ; 28: ebx = ptr to 36-byte buffer -> 0/-1
    cmp ebx, 0
    je .error
    push edx
    push esi
    push edi
    mov eax, [exec_vbase]
    mov [ebx], eax
    mov eax, [exec_pages]
    mov [ebx + 4], eax
    lea esi, [exec_name]
    lea edi, [ebx + 8]
    mov ecx, 32
    rep movsb
    pop edi
    pop esi
    pop edx
    xor eax, eax
    ret
.error:
    mov eax, -1
    ret

sys_itoa:                                ; 29: eax = value, edi = dst -> edi = end ptr
    push eax
    push ebx
    push ecx
    push edx
    mov ecx, 0
    mov ebx, 10
    test eax, eax
    jnz .divide
    mov byte [edi], '0'
    inc edi
    jmp .fin
.divide:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .divide
.emit:
    pop edx
    mov al, dl
    add al, '0'
    mov [edi], al
    inc edi
    loop .emit
.fin:
    mov byte [edi], 0
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

sys_get_config:                          ; 30: ebx = key, edi = dst -> eax = 0/-1
    cmp ebx, 3
    ja .bad
    push ebx
    push ecx
    push edx
    cmp ebx, 0
    jne .c1
    mov eax, [cfg_timezone]
    jmp .write
.c1:
    cmp ebx, 1
    jne .c2
    mov eax, [cfg_datefmt]
    jmp .write
.c2:
    cmp ebx, 2
    jne .c3
    mov eax, [cfg_timefmt]
    jmp .write
.c3:
    mov eax, [cfg_layout]
.write:
    push edi
    mov ecx, 0
    mov ebx, 10
    test eax, eax
    jnz .div
    mov byte [edi], '0'
    inc edi
    jmp .wdone
.div:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .div
.wemit:
    pop edx
    mov al, dl
    add al, '0'
    mov [edi], al
    inc edi
    loop .wemit
.wdone:
    mov byte [edi], 0
    pop edi
    pop edx
    pop ecx
    pop ebx
    xor eax, eax
    ret
.bad:
    mov eax, -1
    ret
