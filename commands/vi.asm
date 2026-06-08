[bits 32]
[org 0x00000000]

%define MODE_CMD        0
%define MODE_INSERT     1
%define BUF_MAX         1024
%define VGA_BASE        0xC00B8000+(80*2)*3

_start:
    ; --- Calculate our Dynamic Base Offset ---
    call .get_base
.get_base:
    pop ebp                 ; ebp = absolute runtime address of .get_base
    sub ebp, .get_base      ; ebp = runtime delta address

    ; --- Fetch global command-line argument ---
    mov ebx, 1
    lea edi, [ebp + filename_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; --- Stat the file ---
    lea esi, [ebp + filename_buf]
    lea edi, [ebp + info_buf]
    mov eax, 15             ; sys_stat
    int 0x80
    cmp eax, -1
    je .new

    ; Ensure it's a regular file (type == 1)
    lea edx, [ebp + info_buf]
    cmp dword [edx], 1
    jne .usage

    ; --- Load file into text buffer ---
    mov esi, [edx + 4]      ; file data pointer from stat
    lea edi, [ebp + text_buf]
    mov ecx, [edx + 8]      ; file size from stat
    mov [ebp + size_var], ecx
    test ecx, ecx
    jz .skip
    cld
    rep movsb
.skip:
    jmp .ready

.new:
    mov dword [ebp + size_var], 0

.ready:
    mov dword [ebp + cursor_var],0
    mov dword [ebp + mode_var], MODE_CMD

.loop:
    call render
.poll:
    mov eax,36
    int 0x80                ; heartbeats
    mov eax, 7              ; sys_get_key
    int 0x80
    test al, al
    jz .poll

    mov ecx, [ebp + mode_var]
    cmp ecx, MODE_INSERT
    je .ins_mode

    ; --- Command Mode ---
    cmp al,';'  ; left  
    je .kh
    cmp al,'\'   ;right 
    je .kl
    cmp al,39    ;down 
    je .kj
    cmp al,'['   ;up 
    je .kk
    cmp al,'i'
    je .ki
    cmp al,'x'
    je .kx
    cmp al,'w'
    je .kw
    cmp al,'q'
    je .kq
    jmp .loop

.kh:
    mov eax, [ebp + cursor_var]
    test eax, eax
    jz .loop
    dec eax
    mov [ebp + cursor_var], eax
    jmp .loop
.kl:
    mov eax, [ebp + cursor_var]
    cmp eax, [ebp + size_var]
    jae .loop
    inc eax
    mov [ebp + cursor_var], eax
    jmp .loop
.kj:
    call move_down
    jmp .loop
.kk:
    call move_up
    jmp .loop
.ki:
    mov dword [ebp + mode_var], MODE_INSERT
    jmp .loop
.kx:
    call delete_at_cursor
    jmp .loop
.kw:
    call save_file
    jmp .loop
.kq:
    mov eax, 4              ; Exit / clear terminal hook
    int 0x80
    ret                     ; Return to kernel

    ; --- Insert Mode ---
.ins_mode:
    cmp al,27              ; ESC
    je .esc
    cmp al,8               ; Backspace
    je .bs
    cmp al,13              ; Enter
    je .nl
    cmp al,' '
    jb .loop
    call insert_char
    jmp .loop
.esc:
    mov dword [ebp + mode_var], MODE_CMD
    jmp .loop
.bs:
    mov eax, [ebp + cursor_var]
    test eax, eax
    jz .loop
    dec eax
    mov [ebp + cursor_var], eax
    call delete_at_cursor
    jmp .loop
.nl:
    mov al, 0x0A            ; \n
    call insert_char
    jmp .loop

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret

; --- insert_char: al = char ---
insert_char:
    pushad
    movzx edx, al
    mov ecx, [ebp + size_var]
    cmp ecx, BUF_MAX
    jae .done
    
    lea esi, [ebp + text_buf]
    add esi, [ebp + size_var]
    dec esi
    mov edi, esi
    inc edi
    mov ecx, [ebp + size_var]
    sub ecx, [ebp + cursor_var]
    test ecx, ecx
    jz .place
    std
    rep movsb
    cld
.place:
    lea edi, [ebp + text_buf]
    add edi, [ebp + cursor_var]
    mov [edi], dl
    inc dword [ebp + cursor_var]
    inc dword [ebp + size_var]
.done:
    popad
    ret

; --- delete_at_cursor ---
delete_at_cursor:
    pushad
    mov eax, [ebp + cursor_var]
    cmp eax, [ebp + size_var]
    jae .done
    lea esi, [ebp + text_buf]
    add esi, eax
    inc esi
    lea edi, [ebp + text_buf]
    add edi, eax
    mov ecx, [ebp + size_var]
    sub ecx, eax
    test ecx, ecx
    jz .dec
    dec ecx
    cld
    rep movsb
.dec:
    dec dword [ebp + size_var]
.done:
    popad
    ret

; --- save_file ---
save_file:
    pushad
    lea esi, [ebp + filename_buf]
    mov eax, 17             ; sys_create
    int 0x80
    
    lea esi, [ebp + filename_buf]
    lea ebx, [ebp + text_buf]
    mov ecx, [ebp + size_var]
    mov eax, 18             ; sys_write
    int 0x80
    popad
    ret

; --- get_col into eax ---
get_col:
    push esi
    push ebx
    mov esi, [ebp + cursor_var]
    xor ebx, ebx
.lp:
    test esi, esi
    jz .done
    lea edx, [ebp + text_buf]
    mov al, [edx + esi - 1]
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
    mov [ebp + col_var], eax
    mov esi, [ebp + cursor_var]
    mov ecx, [ebp + size_var]
.fn:
    cmp esi, ecx
    jae .none
    lea edx, [ebp + text_buf]
    cmp byte [edx + esi], 0x0A
    je .past
    inc esi
    jmp .fn
.past:
    inc esi
    cmp esi, ecx
    ja .none
    mov ebx, esi
.eol:
    cmp esi, ecx
    jae .at_eol
    lea edx, [ebp + text_buf]
    cmp byte [edx + esi], 0x0A
    je .at_eol
    inc esi
    jmp .eol
.at_eol:
    mov eax, ebx
    add eax, [ebp + col_var]
    cmp eax, esi
    jbe .ok
    mov eax, esi
.ok:
    mov [ebp + cursor_var], eax
.none:
    popad
    ret

; --- move_up ---
move_up:
    pushad
    call get_col
    mov [ebp + col_var], eax
    mov eax, [ebp + cursor_var]
.cur:
    test eax, eax
    jz .none
    lea edx, [ebp + text_buf]
    cmp byte [edx + eax - 1], 0x0A
    je .cs_found
    dec eax
    jmp .cur
.cs_found:
    dec eax
    mov ebx, eax
.prev:
    test eax, eax
    jz .ps
    lea edx, [ebp + text_buf]
    cmp byte [edx + eax - 1], 0x0A
    je .ps
    dec eax
    jmp .prev
.ps:
    add eax, [ebp + col_var]
    cmp eax, ebx
    jbe .ok
    mov eax, ebx
.ok:
    mov [ebp + cursor_var], eax
.none:
    popad
    ret

; --- render ---
render:
    pushad
    mov eax, 4              ; clear screen
    int 0x80
    mov eax,29
    int 0x80                ; banner
    mov eax,35
    int 0x80                ; hertz
    lea esi, [ebp + hdr1]
    mov eax, 1              ; sys_print
    int 0x80
    lea esi, [ebp + filename_buf]
    mov eax, 1
    int 0x80
    
    mov ecx, [ebp + mode_var]
    cmp ecx, MODE_INSERT
    je .ins
    lea esi, [ebp + mode_cmd]
    jmp .pr
.ins:
    lea esi, [ebp + mode_ins]
.pr:
    mov eax,1
    int 0x80
    mov eax,3              ; print newline
    int 0x80

    lea esi,[ebp + text_buf]
    mov ecx,[ebp + size_var]
    mov eax,16             ; sys_print_buffer
    int 0x80

    lea esi,[ebp + text_buf]
    mov ecx,[ebp + cursor_var]
    mov ebx,1              ; screen row
    xor edx,edx            ; screen col
.walk:
    test ecx,ecx
    jz .pos
    mov al,[esi]
    cmp al,0x0A
    je .nl
    inc edx
    cmp edx,80
    jb .nxt
    xor edx,edx
    inc ebx
    jmp .nxt
.nl:
    xor edx,edx
    inc ebx
.nxt:
    inc esi
    dec ecx
    jmp .walk
.pos:
    cmp ebx,25
    jae .done
    mov eax,ebx
    imul eax,80
    add eax,edx
    shl eax,1
    mov edi,VGA_BASE
    add edi,eax
    mov byte [edi+1], 0x70  ; cursor attribute
.done:
    popad
    ret

; --- Constant Data Section ---
; (This is where the actual file on disk terminates)
hdr1       db "-- vi ", 0
mode_cmd   db " [CMD] --", 0
mode_ins   db " [INS] --", 0
usage_msg  db "usage: vi <file>", 13, 0

; --- Virtual BSS Section (Costing 0 Bytes on Disk) ---
; We use EQU assignments to map offsets right after the physical file data
_bss_start   equ $
cursor_var   equ _bss_start + 0
size_var     equ _bss_start + 4
mode_var     equ _bss_start + 8
col_var      equ _bss_start + 12
info_buf     equ _bss_start + 16
filename_buf equ _bss_start + 28
text_buf     equ _bss_start + 284
