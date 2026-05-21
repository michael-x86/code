; vi - minimal modal editor.
;
; Command-mode keys:
;   h j k l   move cursor (line-aware)
;   i         enter insert mode
;   x         delete char under cursor
;   w         save (creates file if missing)
;   q         quit (no auto-save)
;
; Insert-mode keys:
;   ESC       back to command mode
;   BACKSPC   delete previous char
;   ENTER     insert newline (0x0A)
;   printable insert at cursor
;
; Buffers live at fixed virtual addresses past the binary so they
; don't bloat the binary (resb would zero-fill in flat-bin mode).

[bits 32]
[org 0xC0700000]

%define MODE_CMD       0
%define MODE_INSERT    1
%define BUF_MAX        1024

%define VGA_BASE       0xC00B8000
%define filename_buf   0xC0710000   ; 256 B
%define text_buf       0xC0711000   ; BUF_MAX
%define info_buf       0xC0712000   ; 12 B (stat result)
%define cursor_var     0xC0712020
%define size_var       0xC0712024
%define mode_var       0xC0712028
%define col_var        0xC071202C

_start:
    mov ebx, 1
    mov edi, filename_buf
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    ; stat the file
    mov esi, filename_buf
    mov edi, info_buf
    mov eax, 15
    int 0x80
    cmp eax, -1
    je .new

    cmp dword [info_buf], 1
    jne .isdir

    mov esi, [info_buf + 4]
    mov edi, text_buf
    mov ecx, [info_buf + 8]
    mov [size_var], ecx
    test ecx, ecx
    jz .skip
    cld
    rep movsb
.skip:
    jmp .ready

.new:
    mov dword [size_var], 0

.ready:
    mov dword [cursor_var], 0
    mov dword [mode_var], MODE_CMD

.loop:
    call render
.poll:
    mov eax, 7
    int 0x80
    test al, al
    jz .poll

    cmp dword [mode_var], MODE_INSERT
    je .ins_mode

    ; --- command mode ---
    cmp al, 'h'
    je .kh
    cmp al, 'l'
    je .kl
    cmp al, 'j'
    je .kj
    cmp al, 'k'
    je .kk
    cmp al, 'i'
    je .ki
    cmp al, 'x'
    je .kx
    cmp al, 'w'
    je .kw
    cmp al, 'q'
    je .kq
    jmp .loop

.kh:
    mov eax, [cursor_var]
    test eax, eax
    jz .loop
    dec eax
    mov [cursor_var], eax
    jmp .loop
.kl:
    mov eax, [cursor_var]
    cmp eax, [size_var]
    jae .loop
    inc eax
    mov [cursor_var], eax
    jmp .loop
.kj:
    call move_down
    jmp .loop
.kk:
    call move_up
    jmp .loop
.ki:
    mov dword [mode_var], MODE_INSERT
    jmp .loop
.kx:
    call delete_at_cursor
    jmp .loop
.kw:
    call save_file
    jmp .loop
.kq:
    mov eax, 4
    int 0x80
    ret

    ; --- insert mode ---
.ins_mode:
    cmp al, 27
    je .esc
    cmp al, 8
    je .bs
    cmp al, 13
    je .nl
    cmp al, ' '
    jb .loop
    call insert_char
    jmp .loop
.esc:
    mov dword [mode_var], MODE_CMD
    jmp .loop
.bs:
    mov eax, [cursor_var]
    test eax, eax
    jz .loop
    dec eax
    mov [cursor_var], eax
    call delete_at_cursor
    jmp .loop
.nl:
    mov al, 0x0A
    call insert_char
    jmp .loop

.usage:
    mov esi, usage_msg
    mov eax, 2
    int 0x80
    ret
.isdir:
    mov esi, isdir_msg
    mov eax, 2
    int 0x80
    ret

; --- insert_char: al = char ---
insert_char:
    pushad
    movzx edx, al                   ; save char
    mov ecx, [size_var]
    cmp ecx, BUF_MAX
    jae .done
    ; shift right: dest = cursor+1..size, src = cursor..size-1
    mov esi, text_buf
    add esi, [size_var]
    dec esi                         ; last valid byte
    mov edi, esi
    inc edi
    mov ecx, [size_var]
    sub ecx, [cursor_var]
    test ecx, ecx
    jz .place
    std
    rep movsb
    cld
.place:
    mov edi, text_buf
    add edi, [cursor_var]
    mov [edi], dl
    inc dword [cursor_var]
    inc dword [size_var]
.done:
    popad
    ret

; --- delete_at_cursor ---
delete_at_cursor:
    pushad
    mov eax, [cursor_var]
    cmp eax, [size_var]
    jae .done
    mov esi, text_buf
    add esi, eax
    inc esi
    mov edi, text_buf
    add edi, eax
    mov ecx, [size_var]
    sub ecx, eax
    test ecx, ecx
    jz .dec
    dec ecx
    cld
    rep movsb
.dec:
    dec dword [size_var]
.done:
    popad
    ret

; --- save_file: ensure exists, then write ---
save_file:
    pushad
    mov esi, filename_buf
    mov eax, 17                     ; create (ignored if exists)
    int 0x80
    mov esi, filename_buf
    mov ebx, text_buf
    mov ecx, [size_var]
    mov eax, 18
    int 0x80
    popad
    ret

; --- get_col into eax: col of cursor on its line ---
get_col:
    push esi
    push ebx
    mov esi, [cursor_var]
    xor ebx, ebx
.lp:
    test esi, esi
    jz .done
    mov al, [text_buf + esi - 1]
    cmp al, 0x0A
    je .done
    inc ebx
    dec esi
    jmp .lp
.done:
    mov eax, ebx
    pop ebx
    pop esi
    ret

; --- move_down ---
move_down:
    pushad
    call get_col
    mov [col_var], eax
    ; find next \n at or after cursor
    mov esi, [cursor_var]
    mov ecx, [size_var]
.fn:
    cmp esi, ecx
    jae .none
    cmp byte [text_buf + esi], 0x0A
    je .past
    inc esi
    jmp .fn
.past:
    inc esi                         ; start of next line
    cmp esi, ecx
    ja .none
    mov ebx, esi                    ; line start
    ; find end of next line (or buf end)
.eol:
    cmp esi, ecx
    jae .at_eol
    cmp byte [text_buf + esi], 0x0A
    je .at_eol
    inc esi
    jmp .eol
.at_eol:
    ; eax = min(line_start + col, line_end)
    mov eax, ebx
    add eax, [col_var]
    cmp eax, esi
    jbe .ok
    mov eax, esi
.ok:
    mov [cursor_var], eax
.none:
    popad
    ret

; --- move_up ---
move_up:
    pushad
    call get_col
    mov [col_var], eax
    ; find current line start
    mov eax, [cursor_var]
.cur:
    test eax, eax
    jz .none
    cmp byte [text_buf + eax - 1], 0x0A
    je .cs_found
    dec eax
    jmp .cur
.cs_found:
    ; eax = current line start, prev separator at eax-1
    dec eax                         ; on the \n
    mov ebx, eax                    ; prev_end (the \n's index)
.prev:
    test eax, eax
    jz .ps
    cmp byte [text_buf + eax - 1], 0x0A
    je .ps
    dec eax
    jmp .prev
.ps:
    add eax, [col_var]
    cmp eax, ebx
    jbe .ok
    mov eax, ebx
.ok:
    mov [cursor_var], eax
.none:
    popad
    ret

; --- render: full screen redraw + cursor highlight ---
render:
    pushad
    mov eax, 4
    int 0x80

    mov esi, hdr1
    mov eax, 1
    int 0x80
    mov esi, filename_buf
    mov eax, 1
    int 0x80
    cmp dword [mode_var], MODE_INSERT
    je .ins
    mov esi, mode_cmd
    jmp .pr
.ins:
    mov esi, mode_ins
.pr:
    mov eax, 1
    int 0x80
    mov eax, 3
    int 0x80

    mov esi, text_buf
    mov ecx, [size_var]
    mov eax, 16
    int 0x80

    ; compute (row, col) of cursor in text area (rows 1..)
    mov esi, text_buf
    mov ecx, [cursor_var]
    mov ebx, 1                      ; row
    xor edx, edx                    ; col
.walk:
    test ecx, ecx
    jz .pos
    mov al, [esi]
    cmp al, 0x0A
    je .nl
    inc edx
    cmp edx, 80
    jb .nxt
    xor edx, edx
    inc ebx
    jmp .nxt
.nl:
    xor edx, edx
    inc ebx
.nxt:
    inc esi
    dec ecx
    jmp .walk
.pos:
    cmp ebx, 25
    jae .done
    mov eax, ebx
    imul eax, 80
    add eax, edx
    shl eax, 1
    mov edi, VGA_BASE
    add edi, eax
    mov byte [edi+1], 0x70
.done:
    popad
    ret

hdr1     db "-- vi ", 0
mode_cmd db " [CMD] --", 13, 0
mode_ins db " [INS] --", 13, 0
usage_msg db "usage: vi <file>", 13, 0
isdir_msg db "vi: is a directory", 13, 0
