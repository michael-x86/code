; =============================================================================
; gui_draw.inc — Drawing helpers and status bar procedures for the GUI
; =============================================================================
; in (implied for all): ebp = position-independent base
; =============================================================================

; =============================================================================
; gui_hline — Horizontal line (1 px tall filled rect)
; in:  ebx = x, ecx = y, esi = w, edx = color
; =============================================================================
gui_hline:
    mov eax, SYS_GFX_FILLRECT
    mov edi, 1
    int 0x80
    ret

; =============================================================================
; gui_vline — Vertical line (1 px wide filled rect)
; in:  ebx = x, ecx = y, esi = h, edx = color
; =============================================================================
gui_vline:
    push esi
    mov eax, SYS_GFX_FILLRECT
    mov edi, esi
    mov esi, 1
    int 0x80
    pop esi
    ret

; =============================================================================
; gui_border — 1-pixel outline rectangle
; in:  ebx = x, ecx = y, esi = w, edi = h, edx = color
; =============================================================================
gui_border:
    pushad
    mov [ebp + gui_bdr_x], ebx
    mov [ebp + gui_bdr_y], ecx
    mov [ebp + gui_bdr_w], esi
    mov [ebp + gui_bdr_h], edi
    mov [ebp + gui_bdr_c], edx

    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gui_bdr_x]
    mov ecx, [ebp + gui_bdr_y]
    mov edx, [ebp + gui_bdr_c]
    mov esi, [ebp + gui_bdr_w]
    mov edi, 1
    int 0x80

    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gui_bdr_x]
    mov ecx, [ebp + gui_bdr_y]
    add ecx, [ebp + gui_bdr_h]
    dec ecx
    mov edx, [ebp + gui_bdr_c]
    mov esi, [ebp + gui_bdr_w]
    mov edi, 1
    int 0x80

    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gui_bdr_x]
    mov ecx, [ebp + gui_bdr_y]
    inc ecx
    mov edx, [ebp + gui_bdr_c]
    mov esi, 1
    mov edi, [ebp + gui_bdr_h]
    sub edi, 2
    int 0x80

    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gui_bdr_x]
    add ebx, [ebp + gui_bdr_w]
    dec ebx
    mov ecx, [ebp + gui_bdr_y]
    inc ecx
    mov edx, [ebp + gui_bdr_c]
    mov esi, 1
    mov edi, [ebp + gui_bdr_h]
    sub edi, 2
    int 0x80

    popad
    ret

; =============================================================================
; gui_fill — Filled rectangle
; in:  ebx = x, ecx = y, esi = w, edi = h, edx = color
; =============================================================================
gui_fill:
    mov eax, SYS_GFX_FILLRECT
    int 0x80
    ret

; =============================================================================
; gui_text — Draw a NUL-terminated string
; in:  ebx = x, ecx = y, edx = color, esi = ptr to string
; =============================================================================
gui_text:
    mov eax, SYS_GFX_STRING
    int 0x80
    ret

; =============================================================================
; gui_draw_dots — Dot-pattern texture (pixel lit where (x+y)%2==0)
; in:  ebx = x, ecx = y, esi = w, edi = h
; =============================================================================
gui_draw_dots:
    pushad
    mov [ebp + gui_dd_x0], ebx
    mov [ebp + gui_dd_y0], ecx
    mov [ebp + gui_dd_w], esi
    mov [ebp + gui_dd_h], edi

    mov ecx, 0
.dd_row:
    mov ebx, 0
.dd_col:
    mov eax, [ebp + gui_dd_x0]
    add eax, ebx
    add eax, [ebp + gui_dd_y0]
    add eax, ecx
    test eax, 1
    jnz .dd_skip

    push ebx
    push ecx
    mov eax, SYS_GFX_PIXEL
    mov ebx, [ebp + gui_dd_x0]
    add ebx, [esp + 4]
    mov ecx, [ebp + gui_dd_y0]
    add ecx, [esp]
    mov edx, GUI_COL_DIM
    int 0x80
    pop ecx
    pop ebx

.dd_skip:
    inc ebx
    cmp ebx, [ebp + gui_dd_w]
    jb .dd_col
    inc ecx
    cmp ecx, [ebp + gui_dd_h]
    jb .dd_row

    popad
    ret

; =============================================================================
; gui_window_draw — Fill background and draw border
; in:  esi = ptr to WINDOW_ST, eax = background color
; =============================================================================
gui_window_draw:
    pushad
    mov [ebp + gui_win_bg], eax
    ; Save all window fields before any syscall (which may clobber esi)
    mov eax, [esi + WINDOW_ST.x]
    mov [ebp + gui_win_x], eax
    mov eax, [esi + WINDOW_ST.y]
    mov [ebp + gui_win_y], eax
    mov eax, [esi + WINDOW_ST.width]
    mov [ebp + gui_win_w], eax
    mov eax, [esi + WINDOW_ST.height]
    mov [ebp + gui_win_h], eax

    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gui_win_x]
    mov ecx, [ebp + gui_win_y]
    mov edx, [ebp + gui_win_bg]
    mov esi, [ebp + gui_win_w]
    mov edi, [ebp + gui_win_h]
    int 0x80

    mov ebx, [ebp + gui_win_x]
    mov ecx, [ebp + gui_win_y]
    mov esi, [ebp + gui_win_w]
    mov edi, [ebp + gui_win_h]
    mov edx, GUI_COL_FG
    call gui_border

    popad
    ret

; =============================================================================
; Status bar helpers — each fills its slot, then draws text
; in:  esi = ptr to NUL-terminated string
; =============================================================================

gui_status_tl:
    push esi
    mov eax, SYS_GFX_FILLRECT
    mov ebx, SB_TL_X
    mov ecx, SB_TL_Y
    mov edx, GUI_COL_SB
    mov esi, SB_TL_W
    mov edi, FONT_H
    int 0x80
    pop esi
    mov eax, SYS_GFX_STRING
    mov ebx, SB_TL_X
    mov ecx, SB_TL_Y
    mov edx, GUI_COL_FG
    int 0x80
    ret

gui_status_tr:
    push esi
    mov eax, SYS_GFX_FILLRECT
    mov ebx, SB_TR_X
    mov ecx, SB_TR_Y
    mov edx, GUI_COL_SB
    mov esi, SB_TR_W
    mov edi, FONT_H
    int 0x80
    pop esi
    push esi
    call _strlen
    mov ebx, SB_TR_X + SB_TR_W
    sub ebx, eax
    and ebx, ~7
    pop esi
    mov eax, SYS_GFX_STRING
    mov ecx, SB_TR_Y
    mov edx, GUI_COL_FG
    int 0x80
    ret

gui_status_bl:
    push esi
    mov eax, SYS_GFX_FILLRECT
    mov ebx, SB_BL_X
    mov ecx, SB_BL_Y
    mov edx, GUI_COL_SB
    mov esi, SB_BL_W
    mov edi, FONT_H
    int 0x80
    pop esi
    mov eax, SYS_GFX_STRING
    mov ebx, SB_BL_X
    mov ecx, SB_BL_Y
    mov edx, GUI_COL_FG
    int 0x80
    ret

gui_status_br:
    push esi
    mov eax, SYS_GFX_FILLRECT
    mov ebx, SB_BR_X
    mov ecx, SB_BR_Y
    mov edx, GUI_COL_SB
    mov esi, SB_BR_W
    mov edi, FONT_H
    int 0x80
    pop esi
    push esi
    call _strlen
    mov ebx, SB_BR_X + SB_BR_W
    sub ebx, eax
    and ebx, ~7
    pop esi
    mov eax, SYS_GFX_STRING
    mov ecx, SB_BR_Y
    mov edx, GUI_COL_FG
    int 0x80
    ret

; =============================================================================
; _strlen — internal helper
; in:  esi = string ptr
; out: eax = length (bytes, not counting NUL)
; =============================================================================
_strlen:
    push esi
    xor eax, eax
.l:
    cmp byte [esi + eax], 0
    je .d
    inc eax
    jmp .l
.d:
    pop esi
    ret

; =============================================================================
; _itoa — signed decimal integer to string
; in:  eax = integer, edi = 12-byte output buffer
; out: buffer filled with signed decimal string, NUL-terminated
; =============================================================================
_itoa:
    pushad
    mov [ebp + gui_itoa_buf], edi
    test eax, eax
    jns .positive
    neg eax
    mov byte [edi], '-'
    inc edi
.positive:
    add edi, 11
    mov byte [edi], 0
.dloop:
    dec edi
    xor edx, edx
    mov ebx, 10
    div ebx
    add dl, '0'
    mov [edi], dl
    test eax, eax
    jnz .dloop
    popad
    ret

; --- Variables for gui_draw.inc procedures ------------------------------------
gui_bdr_x:  dd 0
gui_bdr_y:  dd 0
gui_bdr_w:  dd 0
gui_bdr_h:  dd 0
gui_bdr_c:  dd 0
gui_dd_x0:  dd 0
gui_dd_y0:  dd 0
gui_dd_w:   dd 0
gui_dd_h:   dd 0
gui_itoa_buf: dd 0
gui_win_bg: dd 0
gui_win_x:  dd 0
gui_win_y:  dd 0
gui_win_w:  dd 0
gui_win_h:  dd 0
