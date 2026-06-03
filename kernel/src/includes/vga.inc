; =============================================================================
; vga.inc — VGA text-mode driver
; =============================================================================
;
; Everything that talks to the VGA text framebuffer lives here. The
; buffer is at the higher-half alias 0xC00B8000; each cell is 2 bytes
; (char, attribute). We use green-on-black (0x02) as the default color.
;
; cursor_pos is a byte offset (0..VGA_COLS*VGA_ROWS-1) into the buffer;
; putchar advances it and wraps. The hardware cursor (the blinking
; underscore) is programmed separately via the VGA controller ports.
; This file also owns the boot banner and the "shutdown" stub (the
; shutdown does not belong to commands.inc because it's a single-line
; QEMU isa-debug-exit port write, not a user-facing command — though
; the shell aliases it as the "exit" / "quit" command).
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; putchar — write one character at the cursor, advance
;   in:  al = ASCII character
;
; Color is hardcoded green-on-black (0x02). The hardware cursor is
; updated to track the new position. If the cursor would go past the
; end of the buffer, we scroll the screen up by one line.
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
; newline — move the cursor to the start of the next line
;--
;-- If we were already on the last line, scroll up by one.
; -----------------------------------------------------------------------------
newline:
    push eax
    push ebx
    push edx
    mov eax, [cursor_pos]
    mov ebx, VGA_COLS
    xor edx, edx
    div ebx                              ; eax = row, edx = col
    inc eax
    mul ebx                              ; eax = next-line byte offset
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
; print — write a NUL-terminated string
;   in:  esi = string pointer
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
; print_cr — print a string, treating CR (0x0D) as a newline
;   in:  esi = string pointer
;
; This is what the help text uses, since help_me is stored with
; embedded 0x0D bytes (one per line) for compactness.
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
; cls — clear the screen below the pinned banner, cursor to start of row 1
;-- We use rep stosd to fill rows 1..24 with green-on-black space (0x0720)
;-- two cells at a time. Row 0 is the pinned banner: it is left intact and
;-- repainted so the bar survives a `clear`.
; -----------------------------------------------------------------------------
cls:
    mov edi, VGA_TEXT_BUFFER + VGA_COLS * 2
    mov ecx, VGA_COLS * (VGA_ROWS - 1) / 2
    mov eax, 0x07200720                  ; green-on-black space x 2
    rep stosd
    call banner
    mov dword [cursor_pos], VGA_COLS     ; start of row 1
    call cursor
    ret


; -----------------------------------------------------------------------------
; scroll — shift the screen up by one line, clear the bottom line
;
; Row 0 is the pinned banner bar and is left untouched: we shift rows
; 2..24 up into rows 1..23 and clear row 24.
; -----------------------------------------------------------------------------
scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi, VGA_TEXT_BUFFER + VGA_COLS * 2 * 2
    mov edi, VGA_TEXT_BUFFER + VGA_COLS * 2
    mov ecx, VGA_COLS * (VGA_ROWS - 2)
.lp1:
    mov ax, [esi]
    mov [edi], ax
    add esi, 2
    add edi, 2
    loop .lp1
    mov ecx, VGA_COLS
    mov ax, 0x0720                      ; green-on-black space
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
;--
;-- prompt_limit is the cursor byte-offset of the FIRST character after
;-- the "$ " — delchar uses it to refuse to backspace past the start
;-- of the user's command.
; -----------------------------------------------------------------------------
prompt:
    mov al, '$'
    call putchar
    mov al, ' '
    call putchar
    mov [prompt_limit], ebx
    ret


; -----------------------------------------------------------------------------
; cursor — program the VGA hardware cursor (the blinking underscore)
;--
;-- The VGA controller has two index/data port pairs: 0x3D4/0x3D5 for
;-- the index (0x0E = cursor high, 0x0F = cursor low). The cursor
;-- position is a 16-bit value, written MSB-then-LSB.
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
; delchar — erase the character just before the cursor
;--
;-- Refuses to backspace past prompt_limit so the user can't delete
;-- into the "$ " prompt. The cell at (cursor_pos - 1) is replaced
;-- with a green-on-black space.
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
    mov ax, 0x0020                      ; green-on-black space
    mov [edi], ax
    mov [cursor_pos], ebx
    call cursor
.rt:
    ret


; -----------------------------------------------------------------------------
; banner — paint the boot message across the top row
;
; Row 0 is a solid green bar: every cell is filled with a black-on-green
; space first, then sys_msg is written over the left of the row in black
; text. The tick counter (cols 72-79) is redrawn on top by print_tick in
; the same black-on-green color.
; -----------------------------------------------------------------------------
banner:
    ; Fill the whole top row with black-on-green spaces.
    mov edi, VGA_TEXT_BUFFER
    mov ecx, VGA_COLS
    mov ax, (VGA_COLOR_BANNER << 8) | ' '
.fill:
    mov [edi], ax
    add edi, 2
    loop .fill

    ; Write the banner text over the row, black on green.
    mov esi, sys_msg
    mov edi, VGA_TEXT_BUFFER
    mov ah, VGA_COLOR_BANNER
.next:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .next
.done:
    ret


; -----------------------------------------------------------------------------
; shutdown — exit QEMU via the isa-debug-exit device
;--
;-- The QEMU -device isa-debug-exit,iobase=0xf4,iosize=0x04 maps a port
;-- write to a QEMU exit with status = (value & 0xFF) << 1 | 1. Writing
;-- 0 gives exit status 1 (clean shutdown for the test harness).
; -----------------------------------------------------------------------------
shutdown:
    cli
    mov dx, 0xF4
    mov al, 0
    out dx, al
    hlt
    ret


section .rodata

sys_msg  db  "*** OMNI INDUSTRIES UNIFIED OPERATING SYSTEM ***", 0
