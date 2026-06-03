; =============================================================================
; parser.inc — command-line parser and history ring
; =============================================================================
;
; parse_args turns the raw cmd_buf (a NUL-terminated byte string) into
; argc / argv. It works in-place: it overwrites each space with NUL
; and stores the address of the resulting substring in argv[i]. The
; caller therefore must not modify cmd_buf while iterating argv.
;
; History is a circular buffer of HIST_COUNT_MAX records, each
; HIST_RECORD_SIZE bytes (a copy of cmd_buf at the time the command
; was entered). hist_index is the "next position" cursor for
; hist_back / hist_frwd: 0..count, where count means "newer than
; the most recent entry" (an empty line).
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; parse_args — split cmd_buf on spaces, populate argv
;   out: [argc] = count, argv[0..argc-1] = pointers into cmd_buf
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
;
; Skip the save if the command line is empty. If the buffer is full,
; shift everything up by one record (the oldest record is dropped).
; Always resets hist_index to hist_count so the next hist_back lands
; on the just-saved command.
; -----------------------------------------------------------------------------
save_history:
    pushad
    cmp dword [cmd_len], 0
    je .done

    mov eax, [hist_count]
    cmp eax, HIST_COUNT_MAX
    jb .append

    ; Shift history up by one record to make room.
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
; hist_back — recall the older history entry
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
; hist_frwd — recall the newer history entry (or clear the line)
;
; If the user is already at "newer than most recent", clear the line.
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
;
; Loops cmd_len times calling delchar. cmd_len is decremented as we
; go so delchar's "don't go past prompt_limit" check doesn't stop us
; at exactly the right boundary.
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
