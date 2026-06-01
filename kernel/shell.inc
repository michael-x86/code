; =============================================================================
; shell.inc — VGA output, keyboard input, command parser, built-in commands
; =============================================================================

; ── VGA text output ────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; putchar — write a character to VGA text buffer, advance cursor
;   in: al = character
; -----------------------------------------------------------------------------
putchar:
    push edi
    push edx
    push eax
    mov ebx, [cursor_pos]
    mov edx, VGA_TEXT_BUFFER
    lea edi, [edx + ebx * 2]
    mov ah, VGA_COLOR_GREEN
    mov [edi], ax
    inc ebx
    cmp ebx, VGA_COLS * VGA_ROWS
    jb .st
    call scroll
    mov ebx, VGA_COLS * (VGA_ROWS - 1)
.st:
    mov [cursor_pos], ebx
    call cursor
    pop eax
    pop edx
    pop edi
    ret

; -----------------------------------------------------------------------------
; newline — move cursor to the next line; scroll if at bottom
; -----------------------------------------------------------------------------
newline:
    push eax
    push ebx
    push edx
    mov eax, [cursor_pos]
    mov ebx, VGA_COLS
    xor edx, edx
    div ebx
    inc eax
    mul ebx
    mov [cursor_pos], eax
    cmp eax, VGA_COLS * VGA_ROWS
    jb .st
    call scroll
    mov dword [cursor_pos], VGA_COLS * (VGA_ROWS - 1)
.st:
    call cursor
    pop edx
    pop ebx
    pop eax
    ret

; -----------------------------------------------------------------------------
; print — write a null-terminated string
;   in: esi = string pointer
; -----------------------------------------------------------------------------
print:
    push eax
.next:
    lodsb
    test al, al
    jz .done
    call putchar
    jmp .next
.done:
    pop eax
    ret

; -----------------------------------------------------------------------------
; print_cr — write a string, treating CR (13) as newline
;   in: esi = string pointer
; -----------------------------------------------------------------------------
print_cr:
    push eax
.next:
    lodsb
    test al, al
    jz .done
    cmp al, 13
    je .newln
    call putchar
    jmp .next
.newln:
    call newline
    jmp .next
.done:
    pop eax
    ret

; -----------------------------------------------------------------------------
; cls — clear the entire VGA text buffer
; -----------------------------------------------------------------------------
cls:
    mov edi, VGA_TEXT_BUFFER
    mov ecx, VGA_COLS * VGA_ROWS / 2
    mov eax, 0x07200720      ; green-on-black space
    rep stosd
    xor eax, eax
    mov [cursor_pos], eax
    call cursor
    ret

; -----------------------------------------------------------------------------
; scroll — scroll the VGA buffer up by one line, clear the last line
; -----------------------------------------------------------------------------
scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi, VGA_TEXT_BUFFER + VGA_COLS * 2
    mov edi, VGA_TEXT_BUFFER
    mov ecx, VGA_COLS * (VGA_ROWS - 1)
.lp1:
    mov ax, [esi]
    mov [edi], ax
    add esi, 2
    add edi, 2
    loop .lp1
    mov ecx, VGA_COLS
    mov ax, 0x0720
.lp2:
    mov [edi], ax
    add edi, 2
    loop .lp2
    pop eax
    pop ecx
    pop edi
    pop esi
    ret

; -----------------------------------------------------------------------------
; prompt — print "$ " and record the cursor position as the prompt limit
; -----------------------------------------------------------------------------
prompt:
    mov al, '$'
    call putchar
    mov al, ' '
    call putchar
    mov [prompt_limit], ebx
    ret

; -----------------------------------------------------------------------------
; cursor — program the VGA hardware cursor position
; -----------------------------------------------------------------------------
cursor:
    push eax
    push edx
    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    mov dx, 0x3D5
    mov al, byte [cursor_pos]
    out dx, al
    mov dx, 0x3D4
    mov al, 0x0E
    out dx, al
    mov dx, 0x3D5
    mov al, byte [cursor_pos + 1]
    out dx, al
    pop edx
    pop eax
    ret

; -----------------------------------------------------------------------------
; delchar — erase the character before the cursor
; -----------------------------------------------------------------------------
delchar:
    mov ebx, [cursor_pos]
    cmp ebx, [prompt_limit]
    jbe .rt
    dec ebx
    cmp ebx, [prompt_limit]
    jb .rt
    mov edx, VGA_TEXT_BUFFER
    lea edi, [edx + ebx * 2]
    mov ax, 0x0020
    mov [edi], ax
    mov [cursor_pos], ebx
    call cursor
.rt:
    ret

; ── Keyboard input ─────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; get_key — read a key from the keyboard buffer, translate scancode to ASCII
;   out: al = ASCII character, or 0 if buffer empty
; -----------------------------------------------------------------------------
get_key:
    mov eax, [kbd_tail]
    cmp eax, [kbd_head]
    je .empty
    mov bl, [kbd_buf + eax]
    inc eax
    and eax, KBD_BUF_SIZE - 1
    mov [kbd_tail], eax
    mov al, bl
    test al, al
    jz .empty

    ; Arrow up → history back
    cmp al, 0x48
    jne .check_down
    call hist_back
    xor al, al
    ret
.check_down:
    ; Arrow down → history forward
    cmp al, 0x50
    jne .translate
    call hist_frwd
    xor al, al
    ret
.translate:
    cmp al, 128
    jae .invalid
    movzx eax, al
    cmp byte [kbd_shift], 0
    jne .shifted
.normal:
    mov al, [keymap + eax]
    ret
.shifted:
    mov al, [keymap_shift + eax]
    ret
.empty:
    xor al, al
    ret
.invalid:
    mov al, '?'
    ret

; ── Command-line editing and history ───────────────────────────────────────

; -----------------------------------------------------------------------------
; parse_args — split cmd_buf into null-terminated arguments, store in argv
;   out: [argc] = argument count, argv[] = pointers to each argument
; -----------------------------------------------------------------------------
parse_args:
    mov esi, cmd_buf
    xor ecx, ecx
    mov edi, argv
.skip_spaces:
    mov al, [esi]
    cmp al, ' '
    jne .check
    inc esi
    jmp .skip_spaces
.check:
    test al, al
    jz .done
    mov [edi], esi
    add edi, 4
    inc ecx
.scan:
    mov al, [esi]
    test al, al
    jz .done
    cmp al, ' '
    je .term
    inc esi
    jmp .scan
.term:
    mov byte [esi], 0
    inc esi
    jmp .skip_spaces
.done:
    mov [argc], ecx
    ret

; -----------------------------------------------------------------------------
; save_history — append cmd_buf to the circular history buffer
; -----------------------------------------------------------------------------
save_history:
    pushad
    cmp dword [cmd_len], 0
    je .done

    mov eax, [hist_count]
    cmp eax, HIST_COUNT_MAX
    jb .append

    ; Shift history up by one record to make room
    mov esi, hist_buf + HIST_RECORD_SIZE
    mov edi, hist_buf
    mov ecx, (HIST_COUNT_MAX - 1) * HIST_RECORD_SIZE
    rep movsb
    mov dword [hist_count], HIST_COUNT_MAX - 1
.append:
    mov eax, [hist_count]
    mov ebx, HIST_RECORD_SIZE
    mul ebx
    mov edi, hist_buf
    add edi, eax
    mov esi, cmd_buf
.copy:
    lodsb
    stosb
    test al, al
    jnz .copy
    inc dword [hist_count]
    mov eax, [hist_count]
    mov [hist_index], eax
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; hist_back — recall the older history entry into cmd_buf
; -----------------------------------------------------------------------------
hist_back:
    pushad
    mov eax, [hist_index]
    test eax, eax
    jz .done
    dec eax
    mov [hist_index], eax
    mov ebx, HIST_RECORD_SIZE
    mul ebx
    mov esi, hist_buf
    add esi, eax
    call clear_cmdline
    mov edi, cmd_buf
    xor ecx, ecx
.copy:
    lodsb
    stosb
    test al, al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len], ecx
    mov esi, cmd_buf
.print:
    lodsb
    test al, al
    jz .done
    call putchar
    jmp .print
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; hist_frwd — recall the newer history entry into cmd_buf
; -----------------------------------------------------------------------------
hist_frwd:
    pushad
    mov eax, [hist_index]
    mov ebx, [hist_count]
    cmp eax, ebx
    jae .done
    inc eax
    mov [hist_index], eax
    cmp eax, ebx
    je .empty_line
    mov ecx, HIST_RECORD_SIZE
    mul ecx
    mov esi, hist_buf
    add esi, eax
    call clear_cmdline
    mov edi, cmd_buf
    xor ecx, ecx
.copy:
    lodsb
    stosb
    test al, al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len], ecx
    mov esi, cmd_buf
.print:
    lodsb
    test al, al
    jz .done
    call putchar
    jmp .print
.empty_line:
    call clear_cmdline
    mov dword [cmd_len], 0
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; clear_cmdline — erase the current command line from the screen
; -----------------------------------------------------------------------------
clear_cmdline:
    pushad
.loop:
    cmp dword [cmd_len], 0
    jbe .done
    dec dword [cmd_len]
    call delchar
    jmp .loop
.done:
    popad
    ret

; ── Command dispatch ───────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; dispatch_command — walk cmd_table, match argv[0], call handler
;   Falls through to exec_bin if no built-in matches.
; -----------------------------------------------------------------------------
dispatch_command:
    pushad
    mov esi, cmd_table
.next_cmd:
    cmp byte [esi], 0
    je .not_found
    mov ebx, esi
    mov eax, [argc]
    test eax, eax
    jz .not_found
    mov edi, [argv]
.compare:
    mov al, [ebx]
    mov dl, [edi]
    cmp al, dl
    jne .skip_it
    test al, al
    jz .found
    inc ebx
    inc edi
    jmp .compare
.skip_it:
    cmp byte [esi], 0
    je .after_name
    inc esi
    jmp .skip_it
.after_name:
    inc esi                  ; skip null terminator
    add esi, 4               ; skip handler address
    jmp .next_cmd
.found:
.find_end:
    cmp byte [esi], 0
    je .get_addr
    inc esi
    jmp .find_end
.get_addr:
    inc esi
    mov eax, [esi]
    mov [cmd_exec], eax
    popad
    mov eax, [cmd_exec]
    call eax
    ret
.not_found:
    popad
    call exec_bin
    ret

; ── Tab completion ─────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; tab_complete — complete a partial command from built-ins and /bin/ executables
; -----------------------------------------------------------------------------
tab_complete:
    pushad
    mov ecx, [cmd_len]
    test ecx, ecx
    jz .out

    xor edx, edx             ; match counter
    mov dword [tab_match_count], 0
    mov dword [tab_single_ptr], 0

    ; Scan built-in cmd_table
    mov esi, cmd_table
.bi_loop:
    cmp byte [esi], 0
    je .bi_done
    push esi
    call tab_prefix_match
    pop esi
    test eax, eax
    jz .bi_next
    inc edx
    mov [tab_single_ptr], esi
.bi_next:
.bi_skip:
    cmp byte [esi], 0
    je .bi_skip_end
    inc esi
    jmp .bi_skip
.bi_skip_end:
    inc esi
    add esi, 4
    jmp .bi_loop
.bi_done:

    ; Scan fs_entries for executables in /bin/
    mov esi, fs_entries
    mov ecx, FS_COUNT
.fs_loop:
    test ecx, ecx
    jz .fs_done
    push esi
    push ecx
    call tab_basename
    test eax, eax
    jz .fs_next
    cmp dword [esi + FS_OFS_TYPE], FS_TYPE_EXEC
    jne .fs_next
    push eax
    mov esi, eax
    call tab_prefix_match
    pop esi
    test eax, eax
    jz .fs_next_pop
    inc edx
    mov [tab_single_ptr], esi
    jmp .fs_next_pop
.fs_next_pop:
    pop ecx
    pop esi
    add esi, FS_REC_SIZE
    dec ecx
    jmp .fs_loop
.fs_next:
    pop ecx
    pop esi
    add esi, FS_REC_SIZE
    dec ecx
    jmp .fs_loop
.fs_done:

    mov [tab_match_count], edx
    test edx, edx
    jz .out
    cmp edx, 1
    je .do_complete

    ; Multiple matches: list them all
    call newline
    call tab_print_all_matches
    call prompt
    mov esi, cmd_buf
    mov ecx, [cmd_len]
.reprint:
    test ecx, ecx
    jz .out
    lodsb
    call putchar
    dec ecx
    jmp .reprint

.do_complete:
    mov esi, [tab_single_ptr]
    mov ecx, [cmd_len]
.skip_prefix:
    test ecx, ecx
    jz .append_rest
    cmp byte [esi], 0
    je .out
    inc esi
    dec ecx
    jmp .skip_prefix
.append_rest:
.copy_rest:
    mov al, [esi]
    test al, al
    jz .add_space
    mov ebx, [cmd_len]
    cmp ebx, CMD_BUF_SIZE - 2
    jae .out
    mov [cmd_buf + ebx], al
    inc dword [cmd_len]
    call putchar
    inc esi
    jmp .copy_rest
.add_space:
    mov ebx, [cmd_len]
    cmp ebx, CMD_BUF_SIZE - 1
    jae .out
    mov al, ' '
    mov [cmd_buf + ebx], al
    inc dword [cmd_len]
    call putchar
.out:
    popad
    ret

; -----------------------------------------------------------------------------
; tab_prefix_match — check if cmd_buf is a prefix of the name at esi
;   out: eax = 1 if match, 0 if not
; -----------------------------------------------------------------------------
tab_prefix_match:
    push esi
    push edi
    push ecx
    mov edi, cmd_buf
    mov ecx, [cmd_len]
    test ecx, ecx
    jz .match
.cmp_loop:
    mov al, [esi]
    mov ah, [edi]
    cmp al, ah
    jne .no
    test al, al
    jz .match
    inc esi
    inc edi
    dec ecx
    jnz .cmp_loop
.match:
    pop ecx
    pop edi
    pop esi
    mov eax, 1
    ret
.no:
    pop ecx
    pop edi
    pop esi
    xor eax, eax
    ret

; -----------------------------------------------------------------------------
; tab_basename — extract basename from an fs_entry if it lives under /bin/
;   in:  esi = pointer to fs_entry (vpath at start)
;   out: eax = pointer to basename, or 0 if not under /bin/
; -----------------------------------------------------------------------------
tab_basename:
    push esi
    cmp byte [esi], '/'
    jne .no
    cmp byte [esi + 1], 'b'
    jne .no
    cmp byte [esi + 2], 'i'
    jne .no
    cmp byte [esi + 3], 'n'
    jne .no
    cmp byte [esi + 4], '/'
    jne .no
    lea esi, [esi + 5]
    cmp byte [esi], 0
    je .no
    mov eax, esi
.scan:
    mov al, [esi]
    test al, al
    jz .ok
    cmp al, '/'
    je .no
    inc esi
    jmp .scan
.ok:
    mov eax, [esp]
    lea eax, [eax + 5]
    pop esi
    ret
.no:
    pop esi
    xor eax, eax
    ret

; -----------------------------------------------------------------------------
; tab_print_all_matches — print all tab-completion candidates
; -----------------------------------------------------------------------------
tab_print_all_matches:
    ; Built-ins
    mov esi, cmd_table
.pbi_loop:
    cmp byte [esi], 0
    je .pbi_done
    push esi
    call tab_prefix_match
    pop esi
    test eax, eax
    jz .pbi_skip
    push esi
.pbi_print:
    lodsb
    test al, al
    jz .pbi_after
    call putchar
    jmp .pbi_print
.pbi_after:
    mov al, ' '
    call putchar
    call putchar
    pop esi
.pbi_skip:
.pbi_find_end:
    cmp byte [esi], 0
    je .pbi_end
    inc esi
    jmp .pbi_find_end
.pbi_end:
    inc esi
    add esi, 4
    jmp .pbi_loop
.pbi_done:

    ; FS executables
    mov esi, fs_entries
    mov ecx, FS_COUNT
.pfs_loop:
    test ecx, ecx
    jz .pfs_done
    push esi
    push ecx
    call tab_basename
    test eax, eax
    jz .pfs_next
    cmp dword [esi + FS_OFS_TYPE], FS_TYPE_EXEC
    jne .pfs_next
    push eax
    mov esi, eax
    call tab_prefix_match
    pop esi
    test eax, eax
    jz .pfs_next
    push esi
.pfs_print:
    lodsb
    test al, al
    jz .pfs_sep
    call putchar
    jmp .pfs_print
.pfs_sep:
    mov al, ' '
    call putchar
    call putchar
    pop esi
.pfs_next:
    pop ecx
    pop esi
    add esi, FS_REC_SIZE
    dec ecx
    jmp .pfs_loop
.pfs_done:
    call newline
    ret

; ── Built-in commands ──────────────────────────────────────────────────────

peek_cmd:
    mov esi, deadbeef
    push eax
    call newline
    mov ecx, 16
.loop:
    lodsb
    call print_hex_byte
    mov al, ' '
    call putchar
    loop .loop
    call newline
    call newline
    pop eax
    ret

help_cmd:
    PRINT_STRING_CR help_me
    ret

echo_cmd:
    mov eax, [argc]
    cmp eax, 2
    jl .done
    mov esi, [argv + 4]
.print:
    lodsb
    test al, al
    jz .done
    call putchar
    jmp .print
.done:
    call newline
    ret

; --- alloc <size> — allocate 4KB pages from the heap -----------------------
alloc_cmd:
    mov ecx, [argc]
    cmp ecx, 2
    jne .usage
    mov esi, [argv + 4]
    call asc2int
    mov eax, edx
    test eax, eax
    jz .usage
    add eax, 4095
    shr eax, 12
    mov ecx, eax
    push ecx
    call find_free_virt
    pop ecx
    jc .failed
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
    jc .failed
    mov ebx, eax
    mov eax, edx
    push edx
    push ecx
    mov ecx, PAGE_PRESENT_RW
    call map_page
    pop ecx
    pop edx
    jc .failed
    add edx, PAGE_SIZE
    dec ecx
    jmp .alloc_loop
.done_alloc:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    call register_allocation
    call newline
    PRINT_STRING alloc_mem
    mov eax, edi
    call print_hex_dword
    call newline
    ret
.failed:
    PRINT_STRING_CR out_mem
    ret
.usage:
    PRINT_STRING_CR no_arg
    ret

; --- free <hexaddr> — release a previously allocated range ------------------
free_cmd:
    mov ecx, [argc]
    cmp ecx, 2
    jne .usage
    mov esi, [argv + 4]
    call hex2int
    mov esi, alloc_table
    mov ecx, alloc_table_count
.search:
    cmp [esi], eax
    je .found
    add esi, 8
    loop .search
    PRINT_STRING_CR bad_free_msg
    ret
.found:
    mov ebx, [esi + 4]
    push eax
    call free_pages
    pop eax
    mov dword [esi], 0
    mov dword [esi + 4], 0
    PRINT_STRING_CR free_ok_msg
    ret
.usage:
    PRINT_STRING_CR free_usage_msg
    ret

; --- heap — show current allocations and free space ------------------------
heap_cmd:
    push eax
    push ecx
    push esi
    call newline
    mov esi, alloc_table
    mov ecx, alloc_table_count
.loop:
    mov eax, [esi]
    test eax, eax
    jz .next
    push eax
    mov al, '0'
    call putchar
    mov al, 'x'
    call putchar
    pop eax
    call print_hex_dword
    call newline
.next:
    add esi, 8
    dec ecx
    jnz .loop
    call calc_free_heap
    call print_int_decimal
    PRINT_STRING_CR in_bytes
    pop esi
    pop ecx
    pop eax
    ret

; --- sys — test int 0x80 syscall interface ---------------------------------
sys_cmd:
    mov eax, 3                ; sys_newline
    int 0x80
    mov esi, sys_peek_msg
    mov eax, 1                ; sys_print
    int 0x80
    mov ebx, sys_peek_msg
    mov eax, 10               ; sys_read_mem
    int 0x80
    mov ebx, eax
    mov eax, 5                ; sys_print_hex
    int 0x80
    mov eax, 3
    int 0x80
    ret

; ── Register / stack / hex / decimal display ───────────────────────────────

show_stack:
    pushad
    call newline
    mov ecx, 8
    mov edi, esp
.loop:
    mov eax, [edi]
    call print_hex_dword
    call newline
    add edi, 4
    dec ecx
    jnz .loop
    popad
    call newline
    ret

show_regs:
    pushad
    mov ebp, esp
    call newline
    PRINT_STRING eax_lbl
    mov eax, [ebp + 28]
    call print_hex_dword
    call newline
    PRINT_STRING ecx_lbl
    mov eax, [ebp + 24]
    call print_hex_dword
    call newline
    PRINT_STRING edx_lbl
    mov eax, [ebp + 20]
    call print_hex_dword
    call newline
    PRINT_STRING ebx_lbl
    mov eax, [ebp + 16]
    call print_hex_dword
    call newline
    PRINT_STRING esp_lbl
    mov eax, [ebp + 12]
    call print_hex_dword
    call newline
    PRINT_STRING ebp_lbl
    mov eax, [ebp + 8]
    call print_hex_dword
    call newline
    PRINT_STRING esi_lbl
    mov eax, [ebp + 4]
    call print_hex_dword
    call newline
    PRINT_STRING edi_lbl
    mov eax, [ebp + 0]
    call print_hex_dword
    call newline
    call newline
    popad
    ret

print_hex_byte:
    push eax
    shr al, 4
    and al, 0x0F
    call print_hex_nibble
    mov al, byte [esp]
    and al, 0x0F
    call print_hex_nibble
    pop eax
    ret

print_hex_nibble:
    cmp al, 9
    jbe .num
    add al, 55
    jmp .out
.num:
    add al, '0'
.out:
    call putchar
    ret

print_hex_dword:
    push eax
    push ecx
    mov ecx, 8
.hloop:
    rol dword [esp + 4], 4
    mov al, byte [esp + 4]
    and al, 0x0F
    cmp al, 9
    jbe .num
    add al, 55
    jmp .out
.num:
    add al, '0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

print_int_decimal:
    push eax
    push ebx
    push ecx
    push edx
    mov ecx, 0
    mov ebx, 10
    test eax, eax
    jnz .divide
    mov al, '0'
    call putchar
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
    call putchar
    loop .emit
.fin:
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; ── String / number conversion ─────────────────────────────────────────────

; -----------------------------------------------------------------------------
; asc2int — parse a decimal ASCII string
;   in:  esi = string
;   out: edx = integer value
; -----------------------------------------------------------------------------
asc2int:
    xor edx, edx
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
    ret

; -----------------------------------------------------------------------------
; hex2int — parse a hex ASCII string (optional 0x/0X prefix)
;   in:  esi = string
;   out: eax = integer value
; -----------------------------------------------------------------------------
hex2int:
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
    jz .done_hex
    cmp bl, '0'
    jb .done_hex
    cmp bl, '9'
    jle .digit
    cmp bl, 'A'
    jb .lower
    cmp bl, 'F'
    jle .upper
    cmp bl, 'a'
    jb .done_hex
    cmp bl, 'f'
    ja .done_hex
.lower:
    sub bl, 'a' - 10
    jmp .shift
.upper:
    sub bl, 'A' - 10
    jmp .shift
.digit:
    sub bl, '0'
.shift:
    shl eax, 4
    or eax, ebx
    inc esi
    jmp .parse
.done_hex:
    ret

; ── Misc ───────────────────────────────────────────────────────────────────

shutdown:
    cli
    mov dx, 0xF4
    mov al, 0
    out dx, al
    hlt
    ret

; --- print_tick — display tick_count as 8 hex digits at top-right ------------
print_tick:
    pushad
    mov eax, [tick_count]
    mov edi, VGA_TEXT_BUFFER + (0 * VGA_COLS + 72) * 2
    mov ecx, 8
.next:
    mov edx, eax
    shr edx, 28
    and dl, 0x0F
    add dl, '0'
    cmp dl, '9'
    jbe .store
    add dl, 7
.store:
    mov dh, 0x07
    mov [edi], dx
    shl eax, 4
    add edi, 2
    dec ecx
    jnz .next
    popad
    ret

; --- banner — paint the boot message at the top of the screen ---------------
banner:
    mov esi, sys_msg
    mov edi, VGA_TEXT_BUFFER + (2 * VGA_COLS + 0) * 2
    mov ah, VGA_COLOR_GREEN
.next:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .next
.done:
    ret
