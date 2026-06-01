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

; -----------------------------------------------------------------------------
; set_syscall — install the int 0x80 gate in the IDT (DPL=3, user-callable)
; -----------------------------------------------------------------------------
set_syscall:
    mov eax, syscall_isr
    mov edi, idt_start + 0x80 * 8
    mov word [edi], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], IDT_DPL3       ; present, DPL=3, 32-bit interrupt gate
    shr eax, 16
    mov word [edi + 6], ax
    ret

; -----------------------------------------------------------------------------
; syscall_isr — dispatch to handler via syscall_table[eax]
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

; ── Individual syscall handlers ─────────────────────────────────────────────

sys_putchar:                             ; 0: ebx = char
    mov eax, ebx
    call putchar
    xor eax, eax
    ret

sys_print:                               ; 1: esi = null-terminated string
    call print
    xor eax, eax
    ret

sys_print_cr:                            ; 2: esi = string (CR → newline)
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

sys_print_int:                           ; 6: ebx = unsigned value
    mov eax, ebx
    call print_int_decimal
    xor eax, eax
    ret

sys_get_key:                             ; 7: → eax = ASCII (0 if empty)
    call get_key
    movzx eax, al
    ret

sys_get_tick:                            ; 8: → eax = tick_count
    mov eax, [tick_count]
    ret

sys_shutdown:                            ; 9: does not return
    call shutdown
    ret

sys_read_mem:                            ; 10: ebx = addr → eax = [ebx]
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

sys_chdir:                               ; 12: esi = path → eax = 0/-1
    push esi
    push edi
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .nf
    cmp dword [eax + FS_OFS_TYPE], FS_TYPE_DIR
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

sys_list_dir:                            ; 13: ebx = index, edi = dst → type/-1
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov [tmp_dst], edi
    mov [tmp_left], ebx
    mov esi, fs_entries
    mov ecx, FS_COUNT
.next:
    test ecx, ecx
    jz .nf
    mov edi, cwd_buf
    call basename_if_child
    test eax, eax
    jz .skip
    cmp dword [tmp_left], 0
    je .hit
    dec dword [tmp_left]
.skip:
    add esi, FS_REC_SIZE
    dec ecx
    jmp .next
.hit:
    mov edi, [tmp_dst]
    push esi
    mov esi, eax
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    pop esi
    mov eax, [esi + FS_OFS_TYPE]
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.nf:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov eax, -1
    ret

sys_stat:                                ; 15: esi = path, edi = info(12B) → 0/-1
    push esi
    push edi
    push ebx
    push edx
    mov ebx, edi
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .nf
    mov edx, [eax + FS_OFS_TYPE]
    mov [ebx], edx
    mov edx, [eax + FS_OFS_DATA]
    mov [ebx + 4], edx
    mov edx, [eax + FS_OFS_SIZE]
    mov [ebx + 8], edx
    pop edx
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.nf:
    pop edx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_print_n:                             ; 16: esi = ptr, ecx = count → 0
.lp:
    test ecx, ecx
    jz .done
    mov al, [esi]
    inc esi
    dec ecx
    cmp al, 0x0A
    je .nl
    cmp al, 0x0D
    je .lp                     ; skip bare CR
    cmp al, 0x09
    jne .raw
    mov al, ' '                ; tab → space
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

sys_get_arg:                             ; 14: ebx = index, edi = dst → 0/-1
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

sys_get_ps_info:                         ; 22: ebx = ptr to 16-byte buffer → 0/-1
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

; ── Writable FS syscalls ────────────────────────────────────────────────────

sys_create:                              ; 17: esi = path → 0/-1
    push esi
    push edi
    push ebx
    push ecx
    push edx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jnz .err                   ; already exists

    ; Find first free slot
    mov edi, fs_entries
    mov ecx, FS_COUNT
.scan:
    test ecx, ecx
    jz .err
    cmp byte [edi], 0
    je .got
    add edi, FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    mov ebx, edi
    mov esi, resolve_buf
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    mov dword [ebx + FS_OFS_TYPE], FS_TYPE_FILE
    mov dword [ebx + FS_OFS_SIZE], 0
    mov eax, ebx
    call persist_entry
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.err:
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_write:                               ; 18: esi = path, ebx = buf, ecx = n → 0/-1
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax + FS_OFS_TYPE], FS_TYPE_FILE
    jne .err
    mov ecx, [esp + 0]
    cmp ecx, FS_CAPACITY
    ja .err

    mov esi, [esp + 4]                     ; source buffer
    mov edi, [eax + FS_OFS_DATA]           ; entry data pointer
    cld
    rep movsb
    mov ecx, [esp + 0]
    mov [eax + FS_OFS_SIZE], ecx           ; update size
    call persist_entry

    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_unlink:                              ; 19: esi = path → 0/-1
    push esi
    push edi
    push ebx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax + FS_OFS_TYPE], FS_TYPE_FILE
    jne .err
    mov byte [eax], 0
    mov dword [eax + FS_OFS_TYPE], FS_TYPE_FREE
    mov dword [eax + FS_OFS_SIZE], 0
    call persist_entry
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.err:
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_mkdir:                               ; 20: esi = path → 0/-1
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jnz .err                   ; already exists

    mov edi, fs_entries
    mov ecx, FS_COUNT
.scan:
    test ecx, ecx
    jz .err
    cmp byte [edi], 0
    je .got
    add edi, FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    mov ebx, edi
    mov esi, resolve_buf
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    mov dword [ebx + FS_OFS_TYPE], FS_TYPE_DIR
    mov dword [ebx + FS_OFS_SIZE], 0
    mov eax, ebx
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

sys_rmdir:                               ; 21: esi = path → 0/-1
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax + FS_OFS_TYPE], FS_TYPE_DIR
    jne .err

    ; Check directory is empty
    push eax
    mov edi, resolve_buf
    mov esi, fs_entries
    mov ecx, FS_COUNT
.chk:
    test ecx, ecx
    jz .empty
    call basename_if_child
    test eax, eax
    jnz .notempty
    add esi, FS_REC_SIZE
    dec ecx
    jmp .chk
.notempty:
    pop eax
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret
.empty:
    pop eax
    mov byte [eax], 0
    mov dword [eax + FS_OFS_TYPE], FS_TYPE_FREE
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax, eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax, -1
    ret

; ── Memory management syscalls ─────────────────────────────────────────────

sys_alloc_pages:                         ; 23: ecx = page count → eax = vaddr/-1
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
    ; Free partially allocated pages
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

sys_free_pages:                          ; 24: ebx = vaddr → 0/-1
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

sys_write_mem:                           ; 25: ebx = addr, ecx = byte value → 0/-1
    mov byte [ebx], cl
    xor eax, eax
    ret

sys_hex2int:                             ; 26: esi = string → eax = value
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

sys_asc2int:                             ; 27: esi = string → edx = value
    push eax
    push ebx
    xor edx, edx
    mov ebx, 10
.loop:
    movzx eax, byte [esi]
    test al, al
    jz .done
    cmp al, ' '
    je .done
    sub eax, '0'
    imul edx, edx, 10
    add edx, eax
    inc esi
    jmp .loop
.done:
    pop ebx
    pop eax
    ret
