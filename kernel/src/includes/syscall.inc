; =============================================================================
; syscall.inc — int 0x80 dispatcher and all 36 syscall handlers
; =============================================================================
;
; The syscall ABI is the contract between the kernel and userland. Every
; program in /bin/ is built against it.
;
; Calling convention
; ------------------
;   eax = syscall number (input), return value (output)
;   ebx, ecx, edx, esi, edi = arguments
;   All other GPRs are preserved by every handler.
;   -1 is the universal error return; 0 (or a positive value) is success.
;
; Dispatch
; --------
; The CPU fires INT 0x80 with our trap gate (DPL=3, set in set_syscall).
; syscall_isr bounds-checks eax against SYSCALL_COUNT and jumps to the
; matching syscall_table entry. The dispatcher saves and restores the
; five argument registers itself, so each handler can be a leaf
; function and use them freely.
;
; The 36 numbers
; --------------
; 0–3:    I/O primitives (putchar, print, print_cr, newline)
; 4:      cls
; 5–6:    number formatting (print_hex, print_int)
; 7–8:    input (get_key, get_tick)
; 9:      shutdown
; 10:      raw memory read
; 11–21:  filesystem (getcwd, chdir, list_dir, get_arg, stat, print_n,
;          create, write, unlink, mkdir, rmdir)
; 22:      ps info
; 23–24:   page-level heap (alloc_pages, free_pages)
; 25:      raw memory write
; 26–27:   parsing (hex2int, asc2int)
; 28–31:   introspection (get_proc_info, itoa, get_config, stack_dump)
; 32–33:   malloc-style heap (alloc, dealloc)
; 34–35:   peek/poke (single-byte raw memory access)
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; set_syscall — install the syscall gate at INT 0x80 with DPL=3
;--
; The trap gate type lets ring-3 userland invoke INT 0x80. We use
; IDT_DPL3 (0xEF), which encodes DPL=3 + present + 32-bit + trap.
; -----------------------------------------------------------------------------
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


; -----------------------------------------------------------------------------
; syscall_isr — dispatch
;--
; Saves the five argument registers, calls the handler at
; syscall_table[eax*4], then restores them and iretd. Bad syscall
; numbers return -1.
; -----------------------------------------------------------------------------
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


; ── I/O primitives (0–6) ────────────────────────────────────────────────────

; 0: putchar
;   in:  ebx = char
sys_putchar:
    mov eax, ebx

    ; Check for control characters
    cmp al, 8              ; Backspace?
    je .backspace

    cmp al, 9              ; Tab?
    je .tab

    cmp al, 10             ; Line feed?
    je .newline

    cmp al, 13             ; Carriage return?
    je .newline

    ; Normal character - call putchar
    call putchar
    xor eax, eax
    ret

.backspace:
    ; Move cursor back one position and erase character
    mov ebx, [cursor_pos]
    test ebx, ebx
    jz .done              ; At start, can't backspace

    dec ebx                 ; Move back one character
    mov [cursor_pos], ebx

    ; Print a space to erase the character
    mov eax, ' '
    call putchar

    ; Move cursor back again (putchar advanced it)
    dec dword [cursor_pos]
    call cursor

.done:
    xor eax, eax
    ret

.tab:
    ; Simplified tab: just print spaces until next multiple of 8
    mov ecx, [cursor_pos]
    mov eax, ecx
    mov ebx, VGA_COLS
    xor edx, edx
    div ebx                 ; eax = row, edx = col
    mov ecx, edx            ; ecx = current column
    add ecx, 8
    and ecx, 0xFFFFFFF8    ; Round to next multiple of 8
    cmp ecx, VGA_COLS
    jb .tab_ok
    mov ecx, VGA_COLS       ; Clamp to line width
.tab_ok:
.tab_loop:
    mov eax, [cursor_pos]
    cmp eax, ecx
    jae .tab_done
    mov eax, ' '
    call putchar
    jmp .tab_loop
.tab_done:
    xor eax, eax
    ret

.newline:
    call newline
    xor eax, eax
    ret

; 1: print
;   in:  esi = NUL-terminated string
sys_print:
    call print
    xor eax, eax
    ret

; 2: print_cr
;   in:  esi = string (0x0D acts as newline)
sys_print_cr:
    call print_cr
    xor eax, eax
    ret

; 3: newline
sys_newline:
    call newline
    xor eax, eax
    ret

; 4: cls
sys_cls:
    call cls
    xor eax, eax
    ret

; 5: print_hex
;   in:  ebx = value
sys_print_hex:
    mov eax, ebx
    call print_hex_dword
    xor eax, eax
    ret

; 6: print_int
;   in:  ebx = value
sys_print_int:
    mov eax, ebx
    call print_int_decimal
    xor eax, eax
    ret


; ── Input and time (7–9) ────────────────────────────────────────────────────

; 7: get_key
;   out: eax = ASCII byte (0 if no key)
sys_get_key:
    call get_key
    movzx eax, al
    ret

; 8: get_tick
;   out: eax = tick_count (100 Hz)
sys_get_tick:
    mov eax, [tick_count]
    ret

; 9: shutdown
;   does not return
sys_shutdown:
    call shutdown
    ret


; ── Raw memory + cwd (10–11) ────────────────────────────────────────────────

; 10: read_mem
;   in:  ebx = address
;   out: eax = dword at [ebx]
sys_read_mem:
    mov eax, [ebx]
    ret

; 11: getcwd
;   in:  edi = dst buffer
sys_getcwd:
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


; ── Filesystem (12–21) ──────────────────────────────────────────────────────

; 12: chdir
;   in:  esi = path
;   out: 0 on success, -1 if path is not a directory
sys_chdir:
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

; 13: list_dir
;   in:  ebx = index, edi = dst
;   out: file_type (1=file, 2=dir) on success, -1 on EOF
;
; The directory entry's file_type field is 1 for files and 2 for BOTH
; directories and executables. We disambiguate by loading the child
; inode and looking at its INODE_TYPE — that's the source of truth.
sys_list_dir:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    mov [tmp_dst], edi
    mov esi, cwd_buf
    call fs_lookup_inode
    cmp eax, -1
    je .ld_nf
    mov ebp, eax
    mov ecx, ebx
    xor ebx, ebx
.ld_blk:
    cmp ebx, 12
    jae .ld_nf
    push ecx
    push ebx
    mov ebx, ebp
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
    cmp eax, -1
    jne .ld_read_ok
    add esp, 12
    jmp .ld_nf
.ld_read_ok:
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
    mov esi, [edi]
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
    mov eax, [tmp_dst]
    push esi
    movzx ecx, byte [edi + 6]
    lea esi, [edi + 8]
    push edi
    mov edi, eax
    cld
    rep movsb
    mov byte [edi], 0
    pop edi
    pop esi
    movzx eax, byte [edi + 7]
    cmp al, 2
    jne .ld_type_file
    push ebx
    mov ebx, esi
    call load_inode
    pop ebx
    mov eax, [inode_buf + INOFS_TYPE]
    cmp eax, INODE_DIR
    je .ld_type_dir
.ld_type_file:
    mov eax, 1
    jmp .ld_type_ok
.ld_type_dir:
    mov eax, 2
.ld_type_ok:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
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

; 14: get_arg
;   in:  ebx = index, edi = dst
;   out: 0 on success, -1 if index out of range
sys_get_arg:
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

; 15: stat
;   in:  esi = path, edi = 12-byte info buffer
;   out: 0 on success, -1 on failure
;   info layout: [type:4][size:4][reserved:4]
sys_stat:
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

; 16: print_n
;   in:  esi = buf, ecx = count
;   Translates LF -> newline, CR -> skip, TAB -> space. Other bytes
;   go through putchar verbatim. Used by `cat` for binary-ish input.
sys_print_n:
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

; 17: create
;   in:  esi = path
;   out: 0 on success, -1 on failure
;
; Splits the path at the last '/', looks up (or creates) the parent
; directory, then allocates a new file inode and adds a directory
; entry. Refuses to clobber an existing file (returns -1).
sys_create:
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    jne .sc_err
    mov esi, resolve_buf
    xor ecx, ecx                         ; last slash offset
    xor edx, edx
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
    lea esi, [resolve_buf + ecx + 1]
    mov edi, file_block_buf + 256
    mov [name_ptr], edi
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
    mov [file_bytes_left], ecx
    mov esi, path_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sc_err
    mov ebx, eax
    call ialloc
    cmp eax, -1
    je .sc_err
    mov edx, eax
    mov dword [inode_buf + INOFS_TYPE], INODE_FILE
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

; 18: write
;   in:  esi = path, ebx = buf, ecx = n
;   out: 0 on success, -1 on failure
sys_write:
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    je .sw_err
    mov ebx, eax
    mov esi, [esp + 4]                   ; original ebx (userland buffer)
    mov ecx, [esp]                       ; original ecx (count)
    xor edx, edx
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

; 19: unlink
;   in:  esi = path
;   out: 0 on success, -1 on failure
sys_unlink:
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

; 20: mkdir
;   in:  esi = path
;   out: 0 on success, -1 on failure
sys_mkdir:
    pushad
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup_inode
    cmp eax, -1
    jne .sm_err
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

; 21: rmdir
;   in:  esi = path
;   out: 0 on success, -1 on failure (only succeeds on empty dirs)
sys_rmdir:
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


; ── Task introspection (22) ────────────────────────────────────────────────

; 22: get_ps_info
;   in:  ebx = 16-byte dst
;   out: 0 on success, -1 if ebx is NULL
;   layout: [current_task:4][task0_esp:4][task1_esp:4][task2_esp:4]
sys_get_ps_info:
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


; ── Page-level heap (23–24) ────────────────────────────────────────────────

; 23: alloc_pages
;   in:  ecx = byte count
;   out: eax = vaddr, -1 on failure
sys_alloc_pages:
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

; 24: free_pages
;   in:  ebx = vaddr (base of a previous alloc_pages)
;   out: 0 on success, -1 if vaddr is not in alloc_table
sys_free_pages:
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


; ── Raw memory write (25) ──────────────────────────────────────────────────

; 25: write_mem
;   in:  ebx = addr, ecx = byte value
sys_write_mem:
    mov byte [ebx], cl
    xor eax, eax
    ret


; ── Parsing (26–27) ────────────────────────────────────────────────────────

; 26: hex2int
;   in:  esi = string
;   out: eax = value
sys_hex2int:
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
    or  eax, ebx
    inc esi
    jmp .parse
.done:
    pop edx
    pop ebx
    ret

; 27: asc2int
;   in:  esi = string
;   out: eax = value
sys_asc2int:
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


; ── Introspection (28–31) ──────────────────────────────────────────────────

; 28: get_proc_info
;   in:  ebx = 36-byte dst
;   out: 0 on success, -1 if ebx is NULL
;   layout: [exec_vbase:4][exec_pages:4][exec_name:32 (NUL-padded, no terminator)]
sys_get_proc_info:
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

; 29: itoa
;   in:  eax = value, edi = dst buffer
;   out: edi = pointer to byte after the written NUL
sys_itoa:
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

; 30: get_config
;   in:  ebx = key (0=timezone, 1=datefmt, 2=timefmt, 3=layout)
;   in:  edi = dst buffer
;   out: 0 on success, -1 on bad key
;
;-- Returns the config value as a NUL-terminated decimal string. We
;-- inline the itoa logic because at this point we want the call to
;-- stay in eax-clean and stack-safe territory (userland reads it back).
sys_get_config:
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

; 31: stack_dump
;   Print a register dump and 8 dwords from the syscall stack frame.
;   Useful for diagnosing crashes from userland (ps uses it).
sys_stack_dump:
    call show_regs
    pushad
    call newline
    mov ecx, 8
    mov edi, esp
.sd_loop:
    mov eax, [edi]
    call print_hex_dword
    call newline
    add edi, 4
    dec ecx
    jnz .sd_loop
    popad
    xor eax, eax
    ret


; ── Malloc-style heap (32–33) ──────────────────────────────────────────────

; 32: alloc
;   in:  ebx = page count
;   out: eax = vaddr, -1 on failure
;
;-- Functionally equivalent to sys_alloc_pages but takes page count
;-- instead of byte count. Exists for parity with the reference set.
sys_alloc:
    mov ecx, ebx
    test ecx, ecx
    jz .alloc_bad
    push ecx
    call find_free_virt
    pop ecx
    jc .alloc_bad
    mov edi, eax
    mov edx, eax
.alloc_loop:
    test ecx, ecx
    jz .alloc_done
    push edx
    push ecx
    call alloc_page
    pop ecx
    pop edx
    jc .alloc_failed
    mov ebx, eax
    mov eax, edx
    push edx
    push ecx
    mov ecx, 3
    call map_page
    pop ecx
    pop edx
    jc .alloc_failed
    add edx, 4096
    dec ecx
    jmp .alloc_loop
.alloc_done:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    call register_allocation
    xor eax, eax
    ret
.alloc_failed:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    test ebx, ebx
    jz .alloc_bad
    push ecx
    call free_pages
    pop ecx
.alloc_bad:
    mov eax, -1
    ret

; 33: dealloc
;   in:  ebx = vaddr (base of a previous alloc / alloc_pages)
;   out: 0 on success, -1 if vaddr is not in alloc_table
sys_dealloc:
    push esi
    push ecx
    push ebx
    mov esi, alloc_table
    mov ecx, alloc_table_count
.de_search:
    cmp [esi], ebx
    je .de_found
    add esi, 8
    loop .de_search
    pop ebx
    pop ecx
    pop esi
    mov eax, -1
    ret
.de_found:
    mov ebx, [esi + 4]
    push eax
    call free_pages
    pop eax
    mov dword [esi], 0
    mov dword [esi + 4], 0
    pop ebx
    pop ecx
    pop esi
    xor eax, eax
    ret


; ── Single-byte raw access (34–35) ────────────────────────────────────────

; 34: peek
;   in:  ebx = addr
;   out: eax = byte at [ebx] (zero-extended)
sys_peek:
    movzx eax, byte [ebx]
    ret

; 35: poke
;   in:  ebx = addr, ecx = byte value
sys_poke:
    mov byte [ebx], cl
    xor eax, eax
    ret


; ── Sound (48) ──────────────────────────────────────────────────────────

; 48: sound
;   in:  ebx = frequency (Hz), ecx = duration (in 10ms ticks)
;   out: 0 on success, -1 on invalid frequency
;   Uses PIT channel 2 to generate sound on PC speaker
sys_sound:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    
    ; Check if frequency is valid (must be > 0 and <= 1193180)
    cmp ebx, 0
    je .sound_off
    cmp ebx, 1193180
    ja .invalid
    
    ; Calculate PIT divisor: 1193180 / frequency
    ; Result must fit in 16 bits (max 0xFFFF)
    mov eax, 1193180
    mov edx, 0
    div ebx
    cmp eax, 0
    je .invalid
    cmp eax, 0xFFFF
    ja .invalid
    
    ; Program PIT channel 2 for square wave mode
    ; Send command byte 0xB6 to port 0x43
    mov al, 0xB6        ; 10110110b: channel 2, LSB/MSB, mode 3 (square wave)
    out 0x43, al
    
    ; Write divisor to channel 2 data port (0x42), LSB first
    mov dx, ax          ; Save divisor (16-bit) in dx
    mov al, dl          ; LSB
    out 0x42, al
    mov al, dh          ; MSB
    out 0x42, al
    
    ; Enable PC speaker (set bits 0 and 1 of port 0x61)
    in al, 0x61
    or al, 0x03
    out 0x61, al
    
    jmp .wait
    
.sound_off:
    ; Just turn off the speaker
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    xor eax, eax
    jmp .done
    
.invalid:
    mov eax, -1
    jmp .done
    
.wait:
    ; Wait for duration (ecx = number of 10ms ticks)
    ; tick_count increments at 100 Hz (10ms per tick)
    mov esi, [tick_count]
    add esi, ecx        ; Target tick count
    
.wait_loop:
    mov eax, [tick_count]
    cmp eax, esi
    jb .wait_loop
    
    ; Turn off speaker
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    
    xor eax, eax
    
.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
