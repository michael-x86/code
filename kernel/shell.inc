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
    mov ebx, [keymap_active]
    mov al, [ebx + eax]
    ret
.shifted:
    mov ebx, [keymap_shift_active]
    mov al, [ebx + eax]
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

    ; Scan /bin directory for executables using new VFS
    mov esi, bin_path
    call fs_lookup_inode
    cmp eax, -1
    je .fs_done
    mov ebx, eax             ; ebx = /bin inode
    xor ebp, ebp             ; ebp = directory block index
.fs_blk:
    cmp ebp, 12
    jae .fs_done
    push ebx
    push ebp
    call load_inode
    pop ebp
    pop ebx
    mov eax, [inode_buf + INOFS_DIRECT + ebp * 4]
    test eax, eax
    jz .fs_done
    ; Read directory block
    push ebx
    push ebp
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ebp
    pop ebx
    ; Scan directory entries
    mov edi, dir_block_buf
    mov ecx, BLOCK_SIZE
.fs_ent:
    cmp ecx, 8
    jb .fs_next_blk
    movzx eax, word [edi + 4] ; rec_len
    test eax, eax
    jz .fs_next_blk
    ; Check if executable (file_type == 2)
    cmp byte [edi + 7], 2
    jne .fs_skip_ent
    ; Get name
    push ecx
    push edi
    push eax
    lea esi, [edi + 8]
    call tab_prefix_match
    pop ecx                  ; rec_len
    pop edi
    test eax, eax
    jz .fs_pop_skip
    inc edx
    movzx eax, byte [edi + 6] ; name_len
    ; Copy name to tab_single_ptr area
    push edi
    push ecx
    lea esi, [edi + 8]
    mov edi, file_block_buf + 512  ; temp name area
    movzx ecx, byte [esp + 2]      ; name_len from stack
    ; Actually name_len is at [esp + 2 + 4] after pushes
    ; Let me just use the dirent directly
    pop ecx
    pop edi
    ; tab_single_ptr points to name in dirent
    lea eax, [edi + 8]
    mov [tab_single_ptr], eax
.fs_pop_skip:
    pop ecx
    jmp .fs_skip_adv
.fs_skip_ent:
    movzx eax, word [edi + 4]
.fs_skip_adv:
    sub ecx, eax
    add edi, eax
    jmp .fs_ent
.fs_next_blk:
    inc ebp
    jmp .fs_blk
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

    ; FS executables in /bin
    mov esi, bin_path
    call fs_lookup_inode
    cmp eax, -1
    je .pfs_done
    mov ebx, eax
    xor ebp, ebp
.pfs_blk:
    cmp ebp, 12
    jae .pfs_done
    push ebx
    push ebp
    call load_inode
    pop ebp
    pop ebx
    mov eax, [inode_buf + INOFS_DIRECT + ebp * 4]
    test eax, eax
    jz .pfs_done
    push ebx
    push ebp
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ebp
    pop ebx
    mov edi, dir_block_buf
    mov ecx, BLOCK_SIZE
.pfs_ent:
    cmp ecx, 8
    jb .pfs_next_blk
    movzx eax, word [edi + 4]
    test eax, eax
    jz .pfs_next_blk
    cmp byte [edi + 7], 2    ; executable?
    jne .pfs_skip
    push ecx
    push edi
    push eax
    lea esi, [edi + 8]
    call tab_prefix_match
    pop ecx
    pop edi
    pop ecx
    test eax, eax
    jz .pfs_skip
    push ecx
    push edi
    push esi
    lea esi, [edi + 8]
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
    pop edi
    pop ecx
.pfs_skip:
    movzx eax, word [edi + 4]
    sub ecx, eax
    add edi, eax
    jmp .pfs_ent
.pfs_next_blk:
    inc ebp
    jmp .pfs_blk
.pfs_done:
    call newline
    ret

; ── Built-in commands ──────────────────────────────────────────────────────

; --- fsdump — dump filesystem superblock, inodes, and directory entries ---
fsdump_cmd:
    pushad

    ; Ensure superblock is loaded
    call ensure_fs_ready

    ; ── Superblock header ───────────────────────────────────────────────
    mov esi, fsd_sb_hdr
    call print
    call newline

    ; magic
    mov esi, fsd_lbl_magic
    call print
    mov eax, [sb_buf + SB_MAGIC]
    call print_hex_dword
    call newline

    ; block_size
    mov esi, fsd_lbl_bsz
    call print
    mov eax, [sb_buf + SB_BLOCK_SIZE]
    call print_int_decimal
    call newline

    ; total_blocks
    mov esi, fsd_lbl_tot
    call print
    mov eax, [sb_buf + SB_TOTAL_BLOCKS]
    call print_int_decimal
    call newline

    ; inode_count
    mov esi, fsd_lbl_ino
    call print
    mov eax, [sb_buf + SB_INODE_COUNT]
    call print_int_decimal
    call newline

    ; free_blocks
    mov esi, fsd_lbl_fb
    call print
    mov eax, [sb_buf + SB_FREE_BLOCKS]
    call print_int_decimal
    call newline

    ; free_inodes
    mov esi, fsd_lbl_fi
    call print
    mov eax, [sb_buf + SB_FREE_INODES]
    call print_int_decimal
    call newline

    ; inode_table_lba (FS-relative)
    mov esi, fsd_lbl_ilba
    call print
    mov eax, [sb_buf + SB_INODE_TABLE_LBA]
    call print_int_decimal
    call newline

    ; bitmap_lba (FS-relative)
    mov esi, fsd_lbl_blba
    call print
    mov eax, [sb_buf + SB_BITMAP_LBA]
    call print_int_decimal
    call newline

    ; data_lba (FS-relative)
    mov esi, fsd_lbl_dlba
    call print
    mov eax, [sb_buf + SB_DATA_LBA]
    call print_int_decimal
    call newline

    ; mount_count
    mov esi, fsd_lbl_mnt
    call print
    mov eax, [sb_buf + SB_MOUNT_COUNT]
    call print_int_decimal
    call newline

    ; fs_base_lba (disk LBA where FS region starts)
    mov esi, fsd_lbl_base
    call print
    mov eax, [fs_base_lba]
    call print_int_decimal
    call newline

    ; ── Inode walk ─────────────────────────────────────────────────────
    call newline
    mov esi, fsd_ino_hdr
    call print
    call newline

    xor ebp, ebp               ; ebp = inode number
.ino_loop:
    cmp ebp, [sb_buf + SB_INODE_COUNT]
    jae .ino_done
    mov ebx, ebp
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FREE
    je .ino_next

    ; Print inode number
    mov eax, ebp
    call print_int_decimal
    mov al, ':'
    call putchar
    mov al, ' '
    call putchar

    ; Print type
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    je .ino_is_dir
    mov esi, fsd_type_file
    call print
    jmp .ino_type_done
.ino_is_dir:
    mov esi, fsd_type_dir
    call print
.ino_type_done:

    ; Print size
    mov esi, fsd_lbl_size
    call print
    mov eax, [inode_buf + INOFS_SIZE]
    call print_int_decimal

    ; Print direct block pointers (non-zero only)
    mov esi, fsd_lbl_blks
    call print
    xor ecx, ecx               ; block pointer index
.blk_loop:
    cmp ecx, 12
    jae .blk_done
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .blk_next
    push ecx
    call print_int_decimal
    mov al, ' '
    call putchar
    pop ecx
.blk_next:
    inc ecx
    jmp .blk_loop
.blk_done:
    call newline

    ; If directory, dump entries
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    jne .ino_next
    push ebp
    mov ebx, ebp
    xor ecx, ecx               ; block index within dir
.dir_blk_loop:
    cmp ecx, 12
    jae .dir_done
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .dir_next_blk
    ; Read directory data block
    push ebx
    push ecx
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ecx
    pop ebx
    ; Scan entries
    mov edi, dir_block_buf
    mov edx, BLOCK_SIZE
.dir_ent_loop:
    cmp edx, 8
    jb .dir_next_blk
    movzx eax, word [edi + 4]  ; rec_len
    test eax, eax
    jz .dir_next_blk
    cmp eax, 8
    jb .dir_next_blk
    cmp eax, edx               ; rec_len must not exceed remaining bytes
    ja .dir_next_blk
    ; Bounds-check inode number
    mov eax, [edi]
    cmp eax, [sb_buf + SB_INODE_COUNT]
    jae .dir_skip_ent          ; invalid inode number - skip
    ; Print indent + name
    push edx
    push edi
    push ecx
    push ebx
    mov al, ' '
    call putchar
    call putchar
    call putchar
    call putchar
    lea esi, [edi + 8]
.dir_name_out:
    lodsb
    test al, al
    jz .dir_name_done
    call putchar
    jmp .dir_name_out
.dir_name_done:
    mov al, ' '
    call putchar
    mov al, '-'
    call putchar
    mov al, '>'
    call putchar
    mov al, ' '
    call putchar
    mov eax, [edi]             ; inode number
    call print_int_decimal
    call newline
    pop ebx
    pop ecx
    pop edi
    pop edx
.dir_skip_ent:
    movzx eax, word [edi + 4]  ; rec_len (reload after pop)
    sub edx, eax
    add edi, eax
    jmp .dir_ent_loop
.dir_next_blk:
    inc ecx
    jmp .dir_blk_loop
.dir_done:
    pop ebp

.ino_next:
    inc ebp
    jmp .ino_loop
.ino_done:
    popad
    ret

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

; ── Config loading ─────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; load_config — read /etc/config and set active keymap + date/time settings
;   Parses a simple key=value text file from the virtual filesystem.
;   Recognised keys: layout, timezone, datefmt, timefmt
; -----------------------------------------------------------------------------
load_config:
    pushad

    ; Look up /etc/config using new VFS
    mov esi, config_path
    call fs_lookup_inode
    cmp eax, -1
    je .done                  ; no config file — keep defaults
    ; Read file content into file_block_buf
    mov ebx, eax
    mov edi, file_block_buf
    mov ecx, BLOCK_SIZE       ; read up to 1 block
    xor edx, edx              ; offset = 0
    call file_read
    cmp eax, -1
    je .done
    mov esi, file_block_buf
    mov ecx, eax              ; bytes read = file size for parsing
    test ecx, ecx
    jz .done

    ; Parse line by line
.parse_line:
    cmp ecx, 0
    jle .done

    ; Skip leading whitespace / blank lines
    mov al, [esi]
    cmp al, ' '
    je .skip_char
    cmp al, 9                 ; tab
    je .skip_char
    cmp al, 10                ; LF
    je .skip_char
    cmp al, 13                ; CR
    je .skip_char
    cmp al, '#'
    je .skip_comment
    jmp .read_key

.skip_comment:
    ; Skip until end of line
    cmp ecx, 0
    jle .done
    mov al, [esi]
    inc esi
    dec ecx
    cmp al, 10
    je .parse_line
    cmp al, 13
    je .parse_line
    jmp .skip_comment

.skip_char:
    inc esi
    dec ecx
    jmp .parse_line

.read_key:
    ; Check for known keys
    ; "layout"
    push esi
    push ecx
    mov edi, key_layout
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_layout

    ; "timezone"
    push esi
    push ecx
    mov edi, key_timezone
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_timezone

    ; "datefmt"
    push esi
    push ecx
    mov edi, key_datefmt
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_datefmt

    ; "timefmt"
    push esi
    push ecx
    mov edi, key_timefmt
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_timefmt

    ; Unknown key — skip line
    jmp .skip_to_eol

.do_layout:
    call .skip_value          ; advance esi past '=' and whitespace
    ; Check value: "us" or "sv"
    cmp byte [esi], 'u'
    jne .check_sv
    cmp byte [esi + 1], 's'
    jne .skip_to_eol
    ; US layout (default) — pointers already set
    jmp .skip_to_eol
.check_sv:
    cmp byte [esi], 's'
    jne .skip_to_eol
    cmp byte [esi + 1], 'v'
    jne .skip_to_eol
    ; Swedish layout
    mov dword [keymap_active], keymap_swe
    mov dword [keymap_shift_active], keymap_swe_shift
    mov dword [cfg_layout], 1
    jmp .skip_to_eol

.do_timezone:
    call .skip_value
    ; Parse signed integer
    push esi
    call asc2int
    mov [cfg_timezone], edx
    pop esi
    jmp .skip_to_eol

.do_datefmt:
    call .skip_value
    cmp byte [esi], '0'
    jne .check_fmt1
    mov dword [cfg_datefmt], 0
    jmp .skip_to_eol
.check_fmt1:
    cmp byte [esi], '1'
    jne .check_fmt2
    mov dword [cfg_datefmt], 1
    jmp .skip_to_eol
.check_fmt2:
    cmp byte [esi], '2'
    jne .skip_to_eol
    mov dword [cfg_datefmt], 2
    jmp .skip_to_eol

.do_timefmt:
    call .skip_value
    cmp byte [esi], '0'
    jne .check_time1
    mov dword [cfg_timefmt], 0
    jmp .skip_to_eol
.check_time1:
    cmp byte [esi], '1'
    jne .skip_to_eol
    mov dword [cfg_timefmt], 1
    jmp .skip_to_eol

.skip_to_eol:
    cmp ecx, 0
    jle .done
    mov al, [esi]
    inc esi
    dec ecx
    cmp al, 10
    je .parse_line
    cmp al, 13
    je .parse_line
    jmp .skip_to_eol

.done:
    popad
    ret

; --- helpers for config parsing ----------------------------------------------

; key_match — check if the key at esi matches the null-terminated string at edi
;   in:  esi = position in config file, edi = key name (null-terminated)
;   out: eax = 1 if match (and next char is '='), 0 otherwise
;   Note: esi is NOT advanced on match — caller must skip past '=' after
.key_match:
    push esi
    push edi
.km_loop:
    mov al, [esi]
    mov ah, [edi]
    test ah, ah
    jz .km_check_eq           ; end of key name — check for '='
    cmp al, ah
    jne .km_no
    inc esi
    inc edi
    jmp .km_loop
.km_check_eq:
    cmp al, '='
    jne .km_no
    pop edi
    pop esi
    mov eax, 1
    ret
.km_no:
    pop edi
    pop esi
    xor eax, eax
    ret

; skip_value — advance esi past '=' and any leading whitespace
;   in/out: esi, ecx
.skip_value:
    cmp ecx, 0
    jle .sv_done
    mov al, [esi]
    inc esi
    dec ecx
    cmp al, '='
    jne .skip_value
.sv_ws:
    cmp ecx, 0
    jle .sv_done
    mov al, [esi]
    cmp al, ' '
    je .sv_wskip
    cmp al, 9
    je .sv_wskip
    jmp .sv_done
.sv_wskip:
    inc esi
    dec ecx
    jmp .sv_ws
.sv_done:
    ret

; --- banner — paint the boot message at the top of the screen ---------------
banner:
    mov esi, sys_msg
    ;mov edi, VGA_TEXT_BUFFER + (2 * VGA_COLS + 0) * 2
    lea edi, [VGA_TEXT_BUFFER + (2 * VGA_COLS + 0) * 2] ;This should work much more better
    mov ah, VGA_COLOR_GREEN
.next:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .next
.done:
    ret
