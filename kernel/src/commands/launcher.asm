; =============================================================================
; launcher — GUI app launcher with embedded TACT COMP and MOTION TRACK
; =============================================================================

[bits 32]
[org 0x00000000]

%include "userland.inc"
jmp _start

%include "gui.inc"
%include "gui_draw.inc"

%define CELL_SIZE    84
%define CELL_GAP_H   24
%define CELL_GAP_V   24
%define GRID_COLS    5
%define GRID_ROWS    3
%define GRID_X ((GUI_WIDTH - (GRID_COLS * CELL_SIZE + (GRID_COLS - 1) * CELL_GAP_H)) / 2)
%define GRID_Y (APP_Y + (APP_HEIGHT - (GRID_ROWS * CELL_SIZE + (GRID_ROWS - 1) * CELL_GAP_V)) / 2)

_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

.main:
    call gui_init
    lea edi, [ebp + app_launcher]
    call gui_launch_app

    mov eax, SYS_GFX_EXIT
    int 0x80
    ret

; =============================================================================
; Launcher callbacks
; =============================================================================

launcher_init:
    pushad
    mov dword [ebp + cur_cell], 0
    call launcher_draw_bg
    call launcher_draw_all
    lea esi, [ebp + str_sys_launcher]
    call gui_status_tl
    lea esi, [ebp + str_deploy]
    call gui_status_br
    popad
    ret

launcher_show:
    pushad
    call launcher_draw_all
    lea esi, [ebp + str_sys_launcher]
    call gui_status_tl
    popad
    ret

launcher_key:
    pushad
    cmp bl, KEY_ESC
    je .suppress                   ; ESC is no-op at launcher grid
    mov eax, [ebp + cur_cell]
    push eax                       ; save old cell index
    cmp bl, KEY_UP
    je .up
    cmp bl, KEY_DOWN
    je .down
    cmp bl, KEY_LEFT
    je .left
    cmp bl, KEY_RIGHT
    je .right
    cmp bl, KEY_ENTER
    je .launch
    cmp bl, KEY_SPACE
    je .launch
    cmp bl, 'Q'
    je .quit
    add esp, 4                     ; discard saved old cell
    jmp .done
.up:
    sub eax, 5
    jns .up_set
    add eax, 15
.up_set:
    jmp .move
.down:
    add eax, 5
    cmp eax, 15
    jb .down_set
    sub eax, 15
.down_set:
    jmp .move
.left:
    dec eax
    jns .left_set
    mov eax, 14
.left_set:
    jmp .move
.right:
    inc eax
    cmp eax, 15
    jb .right_set
    xor eax, eax
.right_set:
.move:
    xchg eax, [esp]                ; stack: old_cell, eax = new_cell
    mov [ebp + cur_cell], eax
    pop eax                        ; eax = old_cell
    call launcher_draw_cell_inactive
    mov eax, [ebp + cur_cell]
    call launcher_cell_exists
    test eax, eax
    jz .move_empty
    mov eax, [ebp + cur_cell]
    call launcher_draw_cell_active
    call launcher_update_status
    jmp .done
.move_empty:
    mov eax, [ebp + cur_cell]
    call launcher_draw_cell_empty
    jmp .done
.launch:
    add esp, 4                     ; discard old cell
    mov eax, [ebp + cur_cell]
    call launcher_launch
    call launcher_show
    jmp .done
.quit:
    add esp, 4
    mov eax, SYS_GFX_EXIT
    int 0x80
    popad
    ret
.suppress:
    mov byte [ebp + gui_key_code], 0
    popad
    ret
.done:
    popad
    ret

launcher_tick:
    pushad
    mov eax, [ebp + gui_last_tick]
    lea edi, [ebp + scratch]
    call gui_status_format_tick
    lea esi, [ebp + scratch]
    call gui_status_tr
    popad
    ret

; =============================================================================
; Drawing helpers
; =============================================================================

launcher_draw_bg:
    pushad
    mov ebx, APP_X
    mov ecx, APP_Y
    mov esi, APP_WIDTH
    mov edi, APP_HEIGHT
    call gui_draw_dots
    popad
    ret

launcher_draw_all:
    pushad
    xor eax, eax
.loop:
    push eax
    call launcher_cell_exists
    test eax, eax
    jz .next
    mov eax, [esp]
    cmp eax, [ebp + cur_cell]
    je .next
    mov eax, [esp]
    call launcher_draw_cell_inactive
.next:
    pop eax
    inc eax
    cmp eax, 15
    jb .loop
    mov eax, [ebp + cur_cell]
    call launcher_cell_exists
    test eax, eax
    jz .no_active
    mov eax, [ebp + cur_cell]
    call launcher_draw_cell_active
.no_active:
    popad
    ret

launcher_draw_cell_inactive:
    push eax
    call launcher_cell_xy
    mov [ebp + ldi_svx], ebx
    mov [ebp + ldi_svy], ecx
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_SB
    mov esi, CELL_SIZE
    mov edi, CELL_SIZE
    int 0x80
    mov ebx, [ebp + ldi_svx]
    mov ecx, [ebp + ldi_svy]
    mov esi, CELL_SIZE
    mov edi, CELL_SIZE
    call gui_draw_dots
    mov ebx, [ebp + ldi_svx]
    mov ecx, [ebp + ldi_svy]
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_FG
    mov esi, CELL_SIZE - 1
    mov edi, CELL_SIZE - 1
    int 0x80
    pop eax
    push eax
    call launcher_cell_name
    mov ebx, [ebp + ldi_svx]
    add ebx, (CELL_SIZE - 3 * FONT_W) / 2
    mov ecx, [ebp + ldi_svy]
    add ecx, (CELL_SIZE - FONT_H) / 2
    mov edx, GUI_COL_FG
    call gui_text
    pop eax
    ret

launcher_draw_cell_active:
    push eax
    call launcher_cell_xy
    mov [ebp + ldi_svx], ebx
    mov [ebp + ldi_svy], ecx
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_BG
    mov esi, CELL_SIZE
    mov edi, CELL_SIZE
    int 0x80
    mov ebx, [ebp + ldi_svx]
    mov ecx, [ebp + ldi_svy]
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_ACC
    mov esi, CELL_SIZE - 1
    mov edi, CELL_SIZE - 1
    int 0x80
    mov ebx, [ebp + ldi_svx]
    add ebx, 2
    mov ecx, [ebp + ldi_svy]
    add ecx, 2
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_ACC
    mov esi, CELL_SIZE - 5
    mov edi, CELL_SIZE - 5
    int 0x80
    pop eax
    push eax
    call launcher_cell_name
    mov ebx, [ebp + ldi_svx]
    add ebx, (CELL_SIZE - 3 * FONT_W) / 2
    mov ecx, [ebp + ldi_svy]
    add ecx, (CELL_SIZE - FONT_H) / 2
    mov edx, GUI_COL_ACC
    call gui_text
    pop eax
    ret

launcher_draw_cell_empty:
    push eax
    call launcher_cell_xy
    mov [ebp + ldi_svx], ebx
    mov [ebp + ldi_svy], ecx
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_BG
    mov esi, CELL_SIZE
    mov edi, CELL_SIZE
    int 0x80
    mov ebx, [ebp + ldi_svx]
    mov ecx, [ebp + ldi_svy]
    mov esi, CELL_SIZE
    mov edi, CELL_SIZE
    call gui_draw_dots
    mov ebx, [ebp + ldi_svx]
    mov ecx, [ebp + ldi_svy]
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_ACC
    mov esi, CELL_SIZE - 1
    mov edi, CELL_SIZE - 1
    int 0x80
    pop eax
    ret

; --- launcher_cell_xy: pixel position for a cell index -----------------------
; in:  eax = cell index (0-14)
; out: ebx = x, ecx = y
launcher_cell_xy:
    push eax
    push edx
    xor edx, edx
    mov ecx, 5
    div ecx
    push eax
    mov eax, edx
    mov ebx, CELL_SIZE + CELL_GAP_H
    mul ebx
    add eax, GRID_X
    mov ebx, eax
    pop eax
    mov ecx, CELL_SIZE + CELL_GAP_V
    mul ecx
    add eax, GRID_Y
    mov ecx, eax
    pop edx
    pop eax
    ret

; --- launcher_cell_exists: check if cell has an app --------------------------
launcher_cell_exists:
    push esi
    lea esi, [ebp + app_table]
    shl eax, 3
    add esi, eax
    mov eax, [esi]
    test eax, eax
    jz .no
    mov eax, 1
    pop esi
    ret
.no:
    xor eax, eax
    pop esi
    ret

; --- launcher_cell_name: get name string for cell ----------------------------
launcher_cell_name:
    push eax
    lea esi, [ebp + app_table]
    shl eax, 3
    add esi, eax
    mov esi, [esi + 4]
    pop eax
    ret

; --- launcher_launch: run app at cell index ----------------------------------
launcher_launch:
    push eax
    push esi
    lea esi, [ebp + app_table]
    shl eax, 3
    add esi, eax
    mov eax, [esi]
    test eax, eax
    jz .done
    add eax, ebp
    mov edi, eax
    call gui_launch_app
.done:
    pop esi
    pop eax
    ret

launcher_update_status:
    pushad
    mov eax, [ebp + cur_cell]
    call launcher_cell_name
    lea edi, [ebp + scratch]
.copy:
    lodsb
    test al, al
    jz .done_copy
    stosb
    jmp .copy
.done_copy:
    mov byte [edi], 0
    lea esi, [ebp + scratch]
    call gui_status_bl
    popad
    ret

; =============================================================================
; App registration table
; =============================================================================
app_table:
    dd app_gcalc,  str_gcalc
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd app_gsnake, str_gsnake
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0
    dd 0,          0

; =============================================================================
; APP_ST instances
; =============================================================================
app_launcher:
    istruc APP_ST
        at APP_ST.on_init, dd launcher_init
        at APP_ST.on_show, dd launcher_show
        at APP_ST.on_key,  dd launcher_key
        at APP_ST.on_tick, dd launcher_tick
    iend

app_gcalc:
    istruc APP_ST
        at APP_ST.on_init, dd gcalc_init
        at APP_ST.on_show, dd gcalc_show
        at APP_ST.on_key,  dd gcalc_key
        at APP_ST.on_tick, dd gcalc_tick
    iend

app_gsnake:
    istruc APP_ST
        at APP_ST.on_init, dd gsnake_init
        at APP_ST.on_show, dd gsnake_show
        at APP_ST.on_key,  dd gsnake_key
        at APP_ST.on_tick, dd gsnake_tick
    iend

; =============================================================================
; Strings
; =============================================================================
str_sys_launcher: db "SYS: LAUNCHER", 0
str_deploy:       db "[ENTER] DEPLOY", 0
str_gcalc:        db "CALC  ", 0
str_gsnake:       db "TRACK ", 0

; =============================================================================
; Variables
; =============================================================================
cur_cell:  dd 0
scratch:   times 32 db 0
ldi_svx:   dd 0
ldi_svy:   dd 0

; =============================================================================
; GCALC — TACT COMP (calculator)
; =============================================================================
; A simple 16-button calculator: 4x4 grid of digit and operator keys.
; Display at top shows current value. Amber for ops, green for digits.
; ESC always returns to launcher (handled by gui_run).
; =============================================================================

%define GC_BTN_W 24
%define GC_BTN_H 24
%define GC_GAP   1
%define GC_COLS  4
%define GC_ROWS  4
%define GC_DISP_H 20
%define GC_WIN_W (GC_COLS * (GC_BTN_W + GC_GAP) + 4)
%define GC_WIN_H (GC_DISP_H + 8 + GC_ROWS * (GC_BTN_H + GC_GAP))

gcalc_init:
    pushad
    mov dword [ebp + gc_cur], 0
    mov dword [ebp + gc_stored], 0
    mov byte [ebp + gc_op], 0
    mov byte [ebp + gc_new], 1
    mov byte [ebp + gc_err], 0
    popad
    ret

gcalc_show:
    pushad
    lea edi, [ebp + gc_win]
    mov esi, GC_WIN_W
    mov ebx, GC_WIN_H
    call gui_window_init
    lea esi, [ebp + gc_win]
    mov eax, GUI_COL_SB
    call gui_window_draw
    call gcalc_redraw
    lea esi, [ebp + str_app_gcalc]
    call gui_status_tl
    lea esi, [ebp + str_esc_abort]
    call gui_status_br
    popad
    ret

gcalc_redraw:
    pushad
    ; Display background
    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gc_win + WINDOW_ST.x]
    add ebx, 2
    mov ecx, [ebp + gc_win + WINDOW_ST.y]
    add ecx, 2
    mov edx, GUI_COL_BG
    mov esi, GC_WIN_W - 4
    mov edi, GC_DISP_H
    int 0x80
    ; UNIT: label
    mov eax, SYS_GFX_STRING
    mov ebx, [ebp + gc_win + WINDOW_ST.x]
    add ebx, 4
    mov ecx, [ebp + gc_win + WINDOW_ST.y]
    add ecx, 4
    mov edx, GUI_COL_DIM
    lea esi, [ebp + gc_unit_str]
    int 0x80
    ; Draw all buttons
    pushad
    xor ebx, ebx
.gcb_row:
    xor ecx, ecx
.gcb_col:
    push ebx
    push ecx
    mov eax, ebx
    shl eax, 2
    add eax, ecx
    push eax
    call gcalc_draw_btn
    pop eax
    pop ecx
    pop ebx
    inc ecx
    cmp ecx, 4
    jb .gcb_col
    inc ebx
    cmp ebx, 4
    jb .gcb_row
    popad
    call gcalc_update_display
    popad
    ret

; --- gcalc_draw_btn: draw one button at (row, col) --------------------------
; in: eax = button index (0-15)
gcalc_draw_btn:
    pushad
    push eax
    mov eax, [ebp + gc_win + WINDOW_ST.x]
    add eax, 2
    mov ebx, [esp]
    and ebx, 3
    mov ecx, GC_BTN_W + GC_GAP
    mul ecx
    add eax, ebx                    ; wait, this is wrong
    ; Let me just compute properly
    pop eax
    push eax
    ; col = index & 3, row = index >> 2
    mov ebx, eax
    and ebx, 3                      ; col
    shr eax, 2                      ; row
    ; x = win_x + 2 + col * (BTN_W + GAP)
    mov ecx, [ebp + gc_win + WINDOW_ST.x]
    add ecx, 2
    mov edx, ebx
    mov esi, GC_BTN_W + GC_GAP
    mov eax, edx
    mul esi
    add ecx, eax
    ; y = win_y + 2 + DISP_H + 4 + row * (BTN_H + GAP)
    mov eax, [ebp + gc_win + WINDOW_ST.y]
    add eax, 2 + GC_DISP_H + 4
    mov edx, [esp]
    shr edx, 2
    mov esi, GC_BTN_H + GC_GAP
    mov ebx, edx
    mov eax, ebx
    mul esi
    add eax, [ebp + gc_win + WINDOW_ST.y]
    add eax, 2 + GC_DISP_H + 4
    mov ebx, ecx                    ; ebx = x
    mov ecx, eax                    ; ecx = y

    ; Check if operator (col == 3)
    mov edx, [esp]
    and edx, 3
    cmp edx, 3
    je .op_btn

    ; Digit button
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_BG
    push ebx
    mov esi, GC_BTN_W
    mov edi, GC_BTN_H
    int 0x80
    pop ebx
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_FG
    push ebx
    mov esi, GC_BTN_W - 1
    mov edi, GC_BTN_H - 1
    int 0x80
    pop ebx
    ; Label
    pop eax
    push eax
    shl eax, 2
    lea esi, [ebp + gc_labels]
    add esi, eax
    mov esi, [esi]
    mov eax, SYS_GFX_STRING
    push ebx
    add ebx, (GC_BTN_W - FONT_W) / 2
    add ecx, (GC_BTN_H - FONT_H) / 2
    mov edx, GUI_COL_FG
    int 0x80
    pop ebx
    jmp .btn_done

.op_btn:
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_BG
    push ebx
    mov esi, GC_BTN_W
    mov edi, GC_BTN_H
    int 0x80
    pop ebx
    mov eax, SYS_GFX_RECT
    mov edx, GUI_COL_WARN
    push ebx
    mov esi, GC_BTN_W - 1
    mov edi, GC_BTN_H - 1
    int 0x80
    pop ebx
    pop eax
    push eax
    shl eax, 2
    lea esi, [ebp + gc_labels]
    add esi, eax
    mov esi, [esi]
    mov eax, SYS_GFX_STRING
    push ebx
    add ebx, (GC_BTN_W - FONT_W) / 2
    add ecx, (GC_BTN_H - FONT_H) / 2
    mov edx, GUI_COL_WARN
    int 0x80
    pop ebx

.btn_done:
    pop eax
    popad
    ret

gcalc_key:
    pushad
    cmp bl, '0'
    jb .check_op
    cmp bl, '9'
    ja .check_op

    ; Digit key: 0-9
    sub bl, '0'
    movzx eax, bl                    ; eax = digit
    mov [ebp + gc_tmp], eax          ; save digit

    cmp byte [ebp + gc_err], 0
    jne .done

    cmp byte [ebp + gc_new], 0
    je .append_digit

    ; First digit after clear/op/equals
    mov eax, [ebp + gc_tmp]
    mov [ebp + gc_cur], eax
    mov byte [ebp + gc_new], 0
    jmp .update

.append_digit:
    mov eax, [ebp + gc_cur]
    mov ebx, 10
    mul ebx
    add eax, [ebp + gc_tmp]
    mov [ebp + gc_cur], eax
    jmp .update

.check_op:
    cmp bl, '+'
    je .op
    cmp bl, '-'
    je .op
    cmp bl, '*'
    je .op
    cmp bl, '/'
    je .op
    cmp bl, 'C'
    je .clear
    cmp bl, 'c'
    je .clear
    cmp bl, '='
    je .equals
    cmp bl, KEY_ENTER
    je .equals
    jmp .done

.op:
    mov [ebp + gc_op], bl
    mov eax, [ebp + gc_cur]
    mov [ebp + gc_stored], eax
    mov byte [ebp + gc_new], 1
    mov byte [ebp + gc_err], 0
    call gcalc_update_op_status
    jmp .done

.clear:
    mov dword [ebp + gc_cur], 0
    mov dword [ebp + gc_stored], 0
    mov byte [ebp + gc_op], 0
    mov byte [ebp + gc_new], 1
    mov byte [ebp + gc_err], 0
    jmp .update

.equals:
    cmp byte [ebp + gc_op], 0
    je .done
    mov eax, [ebp + gc_stored]
    mov ecx, [ebp + gc_cur]
    cmp byte [ebp + gc_op], '+'
    jne .try_sub
    add eax, ecx
    jmp .result
.try_sub:
    cmp byte [ebp + gc_op], '-'
    jne .try_mul
    sub eax, ecx
    jmp .result
.try_mul:
    cmp byte [ebp + gc_op], '*'
    jne .try_div
    mul ecx
    jmp .result
.try_div:
    cmp byte [ebp + gc_op], '/'
    jne .done
    test ecx, ecx
    jz .div_zero
    xor edx, edx
    div ecx
    jmp .result
.div_zero:
    mov dword [ebp + gc_cur], 0
    mov byte [ebp + gc_err], 1
    jmp .update
.result:
    mov [ebp + gc_cur], eax
    mov byte [ebp + gc_op], 0
    mov byte [ebp + gc_new], 1
    mov byte [ebp + gc_err], 0
    call gcalc_update_op_status
.update:
    call gcalc_update_display
.done:
    popad
    ret

gcalc_tick:
    ret

gcalc_update_display:
    pushad
    ; Clear display value area (right side)
    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + gc_win + WINDOW_ST.x]
    add ebx, 4 + 6 * FONT_W         ; after "UNIT: "
    mov ecx, [ebp + gc_win + WINDOW_ST.y]
    add ecx, 2
    mov edx, GUI_COL_BG
    mov esi, GC_WIN_W - 4 - 6 * FONT_W - 4
    mov edi, GC_DISP_H
    int 0x80

    cmp byte [ebp + gc_err], 0
    je .show_val
    mov eax, SYS_GFX_STRING
    lea esi, [ebp + gc_fault_str]
    mov ebx, [ebp + gc_win + WINDOW_ST.x]
    add ebx, GC_WIN_W - 4 - 5 * FONT_W
    mov ecx, [ebp + gc_win + WINDOW_ST.y]
    add ecx, 4
    mov edx, GUI_COL_ALERT
    int 0x80
    popad
    ret

.show_val:
    mov eax, [ebp + gc_cur]
    lea edi, [ebp + gc_val_str]
    call _itoa
    push esi
    lea esi, [ebp + gc_val_str]
    call _strlen
    mov ebx, [ebp + gc_win + WINDOW_ST.x]
    add ebx, GC_WIN_W - 4
    sub ebx, eax
    mov ecx, [ebp + gc_win + WINDOW_ST.y]
    add ecx, 4
    mov edx, GUI_COL_WARN
    pop esi
    call gui_text
    popad
    ret

gcalc_update_op_status:
    pushad
    cmp byte [ebp + gc_op], 0
    je .none
    lea esi, [ebp + gc_op_str]
    mov byte [esi], 'O'
    mov byte [esi + 1], 'P'
    mov byte [esi + 2], ':'
    mov byte [esi + 3], ' '
    mov al, [ebp + gc_op]
    mov [esi + 4], al
    mov byte [esi + 5], 0
    lea esi, [ebp + gc_op_str]
    call gui_status_bl
    popad
    ret
.none:
    lea esi, [ebp + gc_op_str]
    mov byte [esi], 0
    call gui_status_bl
    popad
    ret

; --- gcalc data --------------------------------------------------------------
gc_labels:
    dd gc_l_7, gc_l_8, gc_l_9, gc_l_div
    dd gc_l_4, gc_l_5, gc_l_6, gc_l_mul
    dd gc_l_1, gc_l_2, gc_l_3, gc_l_sub
    dd gc_l_0, gc_l_C, gc_l_eq, gc_l_add

gc_l_7:   db "7", 0
gc_l_8:   db "8", 0
gc_l_9:   db "9", 0
gc_l_div: db "/", 0
gc_l_4:   db "4", 0
gc_l_5:   db "5", 0
gc_l_6:   db "6", 0
gc_l_mul: db "*", 0
gc_l_1:   db "1", 0
gc_l_2:   db "2", 0
gc_l_3:   db "3", 0
gc_l_sub: db "-", 0
gc_l_0:   db "0", 0
gc_l_C:   db "C", 0
gc_l_eq:  db "=", 0
gc_l_add: db "+", 0

gc_unit_str:  db "UNIT:", 0
gc_fault_str: db "FAULT", 0

str_app_gcalc: db "APP: TACT COMP", 0
str_esc_abort: db "[ESC] ABORT", 0

; =============================================================================
; GSNAKE — MOTION TRACK (snake game)
; =============================================================================
; 40×21 grid of 8×8 px cells filling the full app area.
; Snake moves at 20 Hz (every 5 ticks at 100 Hz).
; Dots background preserved — only changed cells redrawn.
; =============================================================================

%define SNK_COLS    40
%define SNK_ROWS    21
%define SNK_CELL    8
%define SNK_MOVE_INTERVAL 5

gsnake_init:
    pushad
    call gsnake_reset
    popad
    ret

gsnake_show:
    pushad
    ; Draw dot pattern over entire app area
    mov ebx, APP_X
    mov ecx, APP_Y
    mov esi, APP_WIDTH
    mov edi, APP_HEIGHT
    call gui_draw_dots

    ; Draw initial snake
    call gsnake_draw_all

    ; Place first food
    call gsnake_place_food

    ; Status
    lea esi, [ebp + str_app_gsnake]
    call gui_status_tl
    lea esi, [ebp + str_esc_abort]
    call gui_status_br
    call gsnake_update_score
    popad
    ret

gsnake_key:
    cmp bl, KEY_UP
    je .up
    cmp bl, KEY_DOWN
    je .down
    cmp bl, KEY_LEFT
    je .left
    cmp bl, KEY_RIGHT
    je .right
    ret
.up:
    cmp dword [ebp + snk_dy], 1
    je .no_change
    mov dword [ebp + snk_dx], 0
    mov dword [ebp + snk_dy], -1
    ret
.down:
    cmp dword [ebp + snk_dy], -1
    je .no_change
    mov dword [ebp + snk_dx], 0
    mov dword [ebp + snk_dy], 1
    ret
.left:
    cmp dword [ebp + snk_dx], 1
    je .no_change
    mov dword [ebp + snk_dx], -1
    mov dword [ebp + snk_dy], 0
    ret
.right:
    cmp dword [ebp + snk_dx], -1
    je .no_change
    mov dword [ebp + snk_dx], 1
    mov dword [ebp + snk_dy], 0
    ret
.no_change:
    ret

gsnake_tick:
    pushad
    cmp byte [ebp + snk_alive], 0
    je .done

    dec byte [ebp + snk_timer]
    jnz .done

    mov byte [ebp + snk_timer], SNK_MOVE_INTERVAL
    call gsnake_move

.done:
    popad
    ret

gsnake_reset:
    pushad
    ; Clear snake buffer
    mov ecx, 200 * 2
    lea edi, [ebp + snk_buf]
    xor eax, eax
    rep stosd

    ; Initial snake: 3 segments at (5,10), (4,10), (3,10) moving right
    mov dword [ebp + snk_head], 2
    mov dword [ebp + snk_tail], 0
    lea edi, [ebp + snk_buf]
    mov dword [edi], 5
    mov dword [edi + 4], 10        ; head
    mov dword [edi + 8], 4
    mov dword [edi + 12], 10
    mov dword [edi + 16], 3
    mov dword [edi + 20], 10       ; tail

    mov dword [ebp + snk_dx], 1
    mov dword [ebp + snk_dy], 0
    mov dword [ebp + snk_score], 0
    mov byte [ebp + snk_alive], 1
    mov byte [ebp + snk_timer], SNK_MOVE_INTERVAL
    popad
    ret

; --- gsnake_move: advance snake one step ------------------------------------
gsnake_move:
    pushad

    ; Get head position
    mov eax, [ebp + snk_head]
    shl eax, 3                     ; * 8 (two dwords per entry)
    lea esi, [ebp + snk_buf]
    add esi, eax
    mov ebx, [esi]                  ; head col
    mov ecx, [esi + 4]              ; head row

    ; Compute new head position
    add ebx, [ebp + snk_dx]
    add ecx, [ebp + snk_dy]

    ; Check wall collision
    cmp ebx, 0
    jl .die
    cmp ebx, SNK_COLS
    jge .die
    cmp ecx, 0
    jl .die
    cmp ecx, SNK_ROWS
    jge .die

    ; Check self-collision (skip tail - it will move away)
    push ebx
    push ecx
    call gsnake_check_self
    test eax, eax
    jnz .die_pop

    ; Check food collision
    cmp ebx, [ebp + food_col]
    jne .no_food
    cmp ecx, [ebp + food_row]
    jne .no_food

    ; Eat food
    add dword [ebp + snk_score], 1
    call gsnake_place_food
    call gsnake_update_score
    jmp .advance_head

.no_food:
    ; Erase tail cell (redraw as dots)
    mov eax, [ebp + snk_tail]
    shl eax, 3
    lea esi, [ebp + snk_buf]
    add esi, eax
    mov ebx, [esi]
    mov ecx, [esi + 4]
    call gsnake_draw_dot_cell

    ; Advance tail (unless we ate food, which increases length)
    add dword [ebp + snk_tail], 1
    cmp dword [ebp + snk_tail], 200
    jb .no_tail_wrap
    mov dword [ebp + snk_tail], 0
.no_tail_wrap:

.advance_head:
    ; Advance head
    add dword [ebp + snk_head], 1
    cmp dword [ebp + snk_head], 200
    jb .no_head_wrap
    mov dword [ebp + snk_head], 0
.no_head_wrap:

    ; Write new head position
    mov eax, [ebp + snk_head]
    shl eax, 3
    lea esi, [ebp + snk_buf]
    add esi, eax

    ; esi[0] = col, esi[4] = row
    mov [esi], ebx
    mov [esi + 4], ecx

    ; Redraw new head in cyan
    call gsnake_draw_head

    pop ecx
    pop ebx
    popad
    ret

.die_pop:
    pop ecx
    pop ebx
.die:
    call gsnake_game_over
    popad
    ret

; --- gsnake_check_self: check if (ebx, ecx) collides with snake body --------
; in:  ebx=col, ecx=row
; out: eax=1 if collision, 0 if clear
gsnake_check_self:
    push esi
    push ecx
    push ebx
    mov eax, [ebp + snk_tail]
.loop:
    cmp eax, [ebp + snk_head]
    je .clear
    push eax
    shl eax, 3
    lea esi, [ebp + snk_buf]
    add esi, eax
    cmp ebx, [esi]
    jne .next
    cmp ecx, [esi + 4]
    je .hit
.next:
    pop eax
    inc eax
    cmp eax, 200
    jb .loop
    xor eax, eax
    pop ebx
    pop ecx
    pop esi
    ret
.hit:
    pop eax
    mov eax, 1
    pop ebx
    pop ecx
    pop esi
    ret
.clear:
    xor eax, eax
    pop ebx
    pop ecx
    pop esi
    ret

; --- gsnake_place_food: place food at random empty cell ---------------------
gsnake_place_food:
    pushad
.random:
    mov eax, [ebp + gui_last_tick]
    and eax, 0x7FFF
    xor edx, edx
    mov ebx, SNK_COLS
    div ebx
    mov [ebp + food_col], edx

    mov eax, [ebp + gui_last_tick]
    shr eax, 8
    xor edx, edx
    mov ebx, SNK_ROWS
    div ebx
    mov [ebp + food_row], edx

    mov ebx, [ebp + food_col]
    mov ecx, [ebp + food_row]
    call gsnake_check_self
    test eax, eax
    jnz .random

    ; Draw food (amber 4×4 centered)
    mov eax, [ebp + food_col]
    shl eax, 3
    add eax, APP_X
    add eax, 2                      ; center 4×4 in 8×8
    mov ebx, [ebp + food_row]
    shl ebx, 3
    add ebx, APP_Y
    add ebx, 2
    pushad
    mov eax, SYS_GFX_FILLRECT
    ; ebx=x, ecx=y from above
    mov edx, GUI_COL_WARN
    mov esi, 4
    mov edi, 4
    int 0x80
    popad

    popad
    ret

; --- gsnake_draw_head: draw the head cell in cyan ---------------------------
gsnake_draw_head:
    pushad
    mov eax, [ebp + snk_head]
    shl eax, 3
    lea esi, [ebp + snk_buf]
    add esi, eax
    mov ebx, [esi]
    mov ecx, [esi + 4]
    shl ebx, 3
    add ebx, APP_X
    shl ecx, 3
    add ecx, APP_Y
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_ACC
    mov esi, SNK_CELL
    mov edi, SNK_CELL
    int 0x80
    popad
    ret

; --- gsnake_draw_dot_cell: draw dot pattern at grid cell (ebx, ecx) ---------
gsnake_draw_dot_cell:
    pushad
    shl ebx, 3
    add ebx, APP_X
    shl ecx, 3
    add ecx, APP_Y
    mov esi, SNK_CELL
    mov edi, SNK_CELL
    call gui_draw_dots
    popad
    ret

; --- gsnake_draw_all: redraw all snake segments -----------------------------
gsnake_draw_all:
    pushad
    mov eax, [ebp + snk_tail]
.loop:
    push eax
    shl eax, 3
    lea esi, [ebp + snk_buf]
    add esi, eax
    mov ebx, [esi]
    mov ecx, [esi + 4]
    shl ebx, 3
    add ebx, APP_X
    shl ecx, 3
    add ecx, APP_Y
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_SB
    mov esi, SNK_CELL
    mov edi, SNK_CELL
    int 0x80
    pop eax
    inc eax
    cmp eax, 200
    jb .loop
    call gsnake_draw_head
    popad
    ret

; --- gsnake_game_over: show game over screen --------------------------------
gsnake_game_over:
    pushad
    mov byte [ebp + snk_alive], 0

    ; Flash red for 6 ticks
    mov eax, SYS_GFX_FILLRECT
    mov ebx, APP_X
    mov ecx, APP_Y
    mov edx, GUI_COL_ALERT
    mov esi, APP_WIDTH
    mov edi, APP_HEIGHT
    int 0x80
    mov eax, SYS_GFX_BLIT
    int 0x80
    call gsnake_wait_ticks

    ; Black
    mov eax, SYS_GFX_FILLRECT
    mov edx, GUI_COL_BG
    int 0x80
    mov eax, SYS_GFX_BLIT
    int 0x80
    call gsnake_wait_ticks

    ; "SIGNAL LOST" centered
    mov eax, SYS_GFX_STRING
    lea esi, [ebp + str_signal_lost]
    mov ebx, (GUI_WIDTH - 11 * FONT_W) / 2
    mov ecx, APP_Y + APP_HEIGHT / 2 - FONT_H
    mov edx, GUI_COL_ALERT
    int 0x80

    ; "CONTACTS: NNNN"
    lea esi, [ebp + scratch]
    call gsnake_format_score
    mov eax, SYS_GFX_STRING
    lea esi, [ebp + scratch]
    mov ebx, (GUI_WIDTH - 16 * FONT_W) / 2
    mov ecx, APP_Y + APP_HEIGHT / 2
    mov edx, GUI_COL_WARN
    int 0x80
    mov eax, SYS_GFX_BLIT
    int 0x80

    ; Wait for any key
.wait:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz .wait
    cmp al, KEY_ESC
    je .done_wait

.done_wait:
    call gsnake_reset
    call gsnake_show
    popad
    ret

gsnake_wait_ticks:
    pushad
    mov eax, [ebp + gui_last_tick]
    add eax, 6
.wait:
    mov ebx, [ebp + gui_last_tick]
    cmp ebx, eax
    jb .wait
    popad
    ret

; --- gsnake_format_score: format score as "CONTACTS: NNNN" ------------------
gsnake_format_score:
    pushad
    lea edi, [ebp + scratch]
    lea esi, [ebp + str_contacts_prefix]
.copy_prefix:
    lodsb
    test al, al
    jz .number
    stosb
    jmp .copy_prefix
.number:
    mov eax, [ebp + snk_score]
    mov ecx, 4
    add edi, 4
    mov byte [edi], 0
.digit:
    dec edi
    xor edx, edx
    mov ebx, 10
    div ebx
    add dl, '0'
    mov [edi], dl
    dec ecx
    jnz .digit
    popad
    ret

gsnake_update_score:
    pushad
    lea esi, [ebp + scratch]
    call gsnake_format_score
    call gui_status_bl
    popad
    ret

; --- gsnake data ------------------------------------------------------------
str_app_gsnake:    db "APP: MOTION TRACK", 0
str_signal_lost:   db "SIGNAL LOST", 0
str_contacts_prefix: db "CONTACTS: ", 0

; =============================================================================
; GCALC and GSNAKE — combined variable storage
; =============================================================================
gc_win:    times 4 dd 0               ; WINDOW_ST
gc_cur:    dd 0
gc_stored: dd 0
gc_op:     db 0
gc_new:    db 0
gc_err:    db 0
gc_tmp:    dd 0
gc_val_str: times 16 db 0
gc_op_str: times 8 db 0

snk_buf:   times 400 dd 0             ; ring buffer of 200 (col,row) pairs
snk_head:  dd 0
snk_tail:  dd 0
snk_dx:    dd 0
snk_dy:    dd 0
food_col:  dd 0
food_row:  dd 0
snk_score: dd 0
snk_alive: db 0
snk_timer: db 0
