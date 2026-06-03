; =============================================================================
; shell.inc — command dispatcher, tab completion, /etc/config parser
; =============================================================================
;
; The "shell" in this kernel is just three things:
;--
;   1. dispatch_command — find a built-in matching argv[0] in cmd_table
;      and call it, or fall through to exec_bin (which loads a binary
;      from /bin/).
;--
;   2. tab_complete — when the user hits Tab, complete the current
;      command line against the built-in table and the /bin directory.
;      Single match → auto-complete with a trailing space. Multiple
;      matches → list them all on a fresh line and reprint the prompt.
;--
;   3. load_config — at boot, read /etc/config and apply key=value
;      settings (keymap, timezone, date/time formats).
;--
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; dispatch_command — match argv[0] against cmd_table, call the handler
; -----------------------------------------------------------------------------
; Walks the table. Each entry is "name\0" followed by a 4-byte handler
; address. Names are compared with str_eq. If the table is exhausted
; without a match, we fall through to exec_bin so the user can run a
; /bin/ binary with the same name.
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
    inc esi                              ; skip name NUL
    add esi, 4                           ; skip handler address
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


; -----------------------------------------------------------------------------
; tab_complete — complete the partial command from built-ins and /bin/
; -----------------------------------------------------------------------------
tab_complete:
    pushad
    mov ecx, [cmd_len]
    test ecx, ecx
    jz .out

    xor edx, edx                         ; match counter
    mov dword [tab_match_count], 0
    mov dword [tab_single_ptr], 0

    ; --- Scan built-in cmd_table for matches ----------------------------
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

    ; --- Scan /bin/ executables for matches -----------------------------
    mov esi, bin_path
    call fs_lookup_inode
    cmp eax, -1
    je .fs_done
    mov ebx, eax
    xor ebp, ebp
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
.fs_ent:
    cmp ecx, 8
    jb .fs_next_blk
    movzx eax, word [edi + 4]
    test eax, eax
    jz .fs_next_blk
    cmp byte [edi + 7], 2                ; executable?
    jne .fs_skip_ent
    push ecx
    push edi
    push eax
    lea esi, [edi + 8]
    call tab_prefix_match
    pop ecx
    pop edi
    test eax, eax
    jz .fs_pop_skip
    inc edx
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

    ; --- Multiple matches: list them all, then redraw the prompt --------
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
; tab_prefix_match — does cmd_buf match a prefix of the name at esi?
;   out: eax = 1 if yes, 0 if no
;
; Treats NUL-terminator in the name as the end of the match. The caller's
;-- current command line is in cmd_buf; we use cmd_len to know how many
;-- bytes of prefix to compare.
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
; tab_basename — extract basename from a path if it lives under /bin/
;   in:  esi = path
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
; tab_print_all_matches — list all candidates for the current prefix
; -----------------------------------------------------------------------------
; Prints the built-in matches, then the /bin/ executables, separated
; by two spaces. We don't try to be clever about columns — this is
; the cold path (multi-match only) and a flat list is fine.
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

    ; /bin executables
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
    cmp byte [edi + 7], 2
    jne .pfs_skip
    push ecx
    push edi
    push eax
    lea esi, [edi + 8]
    call tab_prefix_match
    pop ecx
    pop edi
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


; -----------------------------------------------------------------------------
; load_config — read /etc/config and apply key=value settings
; -----------------------------------------------------------------------------
; Recognized keys: layout (us|sv), timezone (int), datefmt (0..2),
; timefmt (0..1). Comments start with '#' and run to end-of-line. Blank
; lines and leading whitespace are ignored. We don't report parse
; errors — unknown keys are silently skipped.
; -----------------------------------------------------------------------------
load_config:
    pushad

    mov esi, config_path
    call fs_lookup_inode
    cmp eax, -1
    je .done                              ; no config — keep defaults
    mov ebx, eax
    mov edi, file_block_buf
    mov ecx, BLOCK_SIZE
    xor edx, edx
    call file_read
    cmp eax, -1
    je .done
    mov esi, file_block_buf
    mov ecx, eax                          ; bytes read = file size
    test ecx, ecx
    jz .done

.parse_line:
    cmp ecx, 0
    jle .done

    mov al, [esi]
    cmp al, ' '
    je .skip_char
    cmp al, 9
    je .skip_char
    cmp al, 10
    je .skip_char
    cmp al, 13
    je .skip_char
    cmp al, '#'
    je .skip_comment
    jmp .read_key

.skip_comment:
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
    push esi
    push ecx
    mov edi, key_layout
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_layout

    push esi
    push ecx
    mov edi, key_timezone
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_timezone

    push esi
    push ecx
    mov edi, key_datefmt
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_datefmt

    push esi
    push ecx
    mov edi, key_timefmt
    call .key_match
    pop ecx
    pop esi
    test eax, eax
    jnz .do_timefmt

    jmp .skip_to_eol

.do_layout:
    call .skip_value
    cmp byte [esi], 'u'
    jne .check_sv
    cmp byte [esi + 1], 's'
    jne .skip_to_eol
    jmp .skip_to_eol                      ; US layout is the default
.check_sv:
    cmp byte [esi], 's'
    jne .skip_to_eol
    cmp byte [esi + 1], 'v'
    jne .skip_to_eol
    mov dword [keymap_active], keymap_se_normal
    mov dword [keymap_shift_active], keymap_se_shift
    mov dword [cfg_layout], 1
    jmp .skip_to_eol

.do_timezone:
    call .skip_value
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

; --- helpers for config parsing --------------------------------------------

; key_match — does the input at esi start with the literal at edi, then '='?
;   in:  esi = input position, edi = literal (NUL-terminated)
;   out: eax = 1 on match, 0 otherwise. esi is left at the byte after '='.
.key_match:
    push esi
    push edi
.km_loop:
    mov al, [esi]
    mov ah, [edi]
    test ah, ah
    jz .km_check_eq
    cmp al, ah
    jne .km_no
    inc esi
    inc edi
    jmp .km_loop
.km_check_eq:
    cmp al, '='
    jne .km_no
    inc esi                                ; advance past '='
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


section .rodata

; Path strings used by the shell and config loader.
config_path    db "/etc/config", 0
bin_path       db "/bin", 0
key_layout     db "layout", 0
key_timezone   db "timezone", 0
key_datefmt    db "datefmt", 0
key_timefmt    db "timefmt", 0
