; =============================================================================
; gui.inc — GUI framework constants, structs, and core procedures
; =============================================================================
; Include after userland.inc in any GUI program.
; Provides event loop, app lifecycle, screen layout, and color theme.
;
; Conventions:
;   ebp = position-independent base (set by USERLAND_START)
;   edi = position-independent ptr to current APP_ST in gui_run/launch
;   All drawing helpers in gui_draw.inc use the same ABI.
; =============================================================================

; --- Syscall constants --------------------------------------------------------
%define SYS_GETKEY        7
%define SYS_TICK          8
%define SYS_GFX_ENTER     36
%define SYS_GFX_EXIT      37
%define SYS_GFX_CLEAR     38
%define SYS_GFX_PIXEL     39
%define SYS_GFX_FILLRECT  40
%define SYS_GFX_RECT      41
%define SYS_GFX_LINE      42
%define SYS_GFX_CHAR      43
%define SYS_GFX_STRING    44
%define SYS_GFX_BLIT      45
%define SYS_GFX_INFO      46
%define SYS_MOUSE         47

; --- Key codes (from sys_get_key, returns ASCII in al; arrows as 0xF2-0xF5) ---
%define KEY_ESC     0x1B
%define KEY_ENTER   0x0D
%define KEY_SPACE   0x20
%define KEY_BACKSP  0x08
%define KEY_TAB     0x09
%define KEY_UP      0xF2
%define KEY_DOWN    0xF3
%define KEY_LEFT    0xF4
%define KEY_RIGHT   0xF5

; --- Theme colors (3-3-2 palette indices) ------------------------------------
GUI_COL_BG    equ 0x00   ; black        — main background
GUI_COL_FG    equ 0x1C   ; phos. green  — text, borders, inactive UI
GUI_COL_ACC   equ 0x1F   ; bright cyan  — selected / active element
GUI_COL_DIM   equ 0x08   ; dim green    — dot patterns, secondary text
GUI_COL_SB    equ 0x04   ; dark green   — status bar background tint
GUI_COL_WARN  equ 0xFC   ; amber        — operators, warnings
GUI_COL_ALERT equ 0xE0   ; red          — errors, game-over, critical

; --- Screen layout constants --------------------------------------------------
GUI_WIDTH      equ 640
GUI_HEIGHT     equ 400
STATUS_HEIGHT  equ 16
APP_Y          equ STATUS_HEIGHT          ; 16
APP_HEIGHT     equ GUI_HEIGHT - STATUS_HEIGHT * 2  ; 368
APP_X          equ 0
APP_WIDTH      equ GUI_WIDTH
FONT_W         equ 8
FONT_H         equ 16

; --- Status bar slot positions ------------------------------------------------
SB_TL_X       equ 4
SB_TL_Y       equ 4
SB_TL_W       equ 312
SB_TR_X       equ 324
SB_TR_Y       equ 4
SB_TR_W       equ 312
SB_BL_X       equ 4
SB_BL_Y       equ 388
SB_BL_W       equ 312
SB_BR_X       equ 324
SB_BR_Y       equ 388
SB_BR_W       equ 312

; =============================================================================
; Structs
; =============================================================================
struc APP_ST
    .on_init:  resd 1   ; ptr — called once before first on_show (0 = skip)
    .on_show:  resd 1   ; ptr — called when app becomes visible
    .on_key:   resd 1   ; ptr — (key ASCII in bl)
    .on_tick:  resd 1   ; ptr — called once per 100 Hz tick
endstruc                ; 16 bytes

struc WINDOW_ST
    .x:        resd 1
    .y:        resd 1
    .width:    resd 1
    .height:   resd 1
endstruc                ; 16 bytes

; =============================================================================
; gui_init — Enter graphics mode, draw background, separators, and status bars
; =============================================================================
; in:   ebp = position-independent base
; out:  graphics mode active, screen initialized
; =============================================================================
gui_init:
    mov eax, SYS_GFX_ENTER
    int 0x80

    mov eax, SYS_GFX_CLEAR
    mov ebx, GUI_COL_BG
    int 0x80

    ; Top status bar background
    mov eax, SYS_GFX_FILLRECT
    mov ebx, 0
    mov ecx, 0
    mov edx, GUI_COL_SB
    mov esi, GUI_WIDTH
    mov edi, STATUS_HEIGHT
    int 0x80

    ; Bottom status bar background
    mov eax, SYS_GFX_FILLRECT
    mov ebx, 0
    mov ecx, GUI_HEIGHT - STATUS_HEIGHT
    mov edx, GUI_COL_SB
    mov esi, GUI_WIDTH
    mov edi, STATUS_HEIGHT
    int 0x80

    ; Top separator line (2 px tall, at y = STATUS_HEIGHT - 2)
    mov eax, SYS_GFX_FILLRECT
    mov ebx, 0
    mov ecx, STATUS_HEIGHT - 2
    mov edx, GUI_COL_FG
    mov esi, GUI_WIDTH
    mov edi, 2
    int 0x80

    ; Bottom separator line (2 px tall, at y = GUI_HEIGHT - STATUS_HEIGHT)
    mov eax, SYS_GFX_FILLRECT
    mov ebx, 0
    mov ecx, GUI_HEIGHT - STATUS_HEIGHT
    mov edx, GUI_COL_FG
    mov esi, GUI_WIDTH
    mov edi, 2
    int 0x80

    mov eax, SYS_GFX_BLIT
    int 0x80
    ret

; =============================================================================
; gui_run — Main event loop
; =============================================================================
; in:   edi = position-independent ptr to current APP_ST (ebp + offset)
;       ebp = position-independent base
; out:  returns when ESC is pressed (key is consumed)
; =============================================================================
gui_run:
    push ebp
    push edi

.loop:
    mov eax, SYS_TICK
    int 0x80
    cmp eax, [ebp + gui_last_tick]
    je .loop
    mov [ebp + gui_last_tick], eax

    mov eax, [edi + APP_ST.on_tick]
    test eax, eax
    jz .keys
    call eax

.keys:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz .blit
    mov [ebp + gui_key_code], al   ; save for possible suppression by on_key
    mov bl, al
    mov eax, [edi + APP_ST.on_key]
    test eax, eax
    jz .check_esc
    call eax
.check_esc:
    cmp byte [ebp + gui_key_code], KEY_ESC
    je .done

.blit:
    mov eax, SYS_GFX_BLIT
    int 0x80
    jmp .loop

.done:
    pop edi
    pop ebp
    ret

; =============================================================================
; gui_launch_app — Initialize, show, and run an app
; =============================================================================
; in:   edi = ptr to APP_ST (position-independent via ebp)
;       ebp = position-independent base
; =============================================================================
gui_launch_app:
    mov eax, [edi + APP_ST.on_init]
    test eax, eax
    jz .show
    call eax
    mov dword [edi + APP_ST.on_init], 0

.show:
    mov eax, [edi + APP_ST.on_show]
    test eax, eax
    jz .run
    call eax

.run:
    call gui_run
    ret

; =============================================================================
; gui_window_init — Center a window in the app area
; =============================================================================
; in:   edi = ptr to WINDOW_ST
;       esi = desired width
;       ebx = desired height
; out:  WINDOW_ST fields populated, window centered in app area
; =============================================================================
gui_window_init:
    push eax
    push ebx
    push esi

    mov [edi + WINDOW_ST.width], esi
    mov [edi + WINDOW_ST.height], ebx

    mov eax, GUI_WIDTH
    sub eax, esi
    shr eax, 1
    mov [edi + WINDOW_ST.x], eax

    mov eax, APP_HEIGHT
    sub eax, ebx
    shr eax, 1
    add eax, APP_Y
    mov [edi + WINDOW_ST.y], eax

    pop esi
    pop ebx
    pop eax
    ret

; =============================================================================
; gui_status_format_tick — Format tick count as "T+NNNNNN"
; =============================================================================
; in:   eax = tick count
;       edi = 8-byte output buffer
; out:  buffer filled with "T+000000" style string, NUL-terminated
; =============================================================================
gui_status_format_tick:
    pushad
    mov ecx, 6
    add edi, 7
    mov byte [edi], 0

.digit_loop:
    dec edi
    xor edx, edx
    mov ebx, 10
    div ebx
    add dl, '0'
    mov [edi], dl
    dec ecx
    jnz .digit_loop

    dec edi
    mov byte [edi], '+'
    dec edi
    mov byte [edi], 'T'

    popad
    ret

; --- Variables for gui.inc (used by gui_run) ----------------------------------
gui_last_tick: dd 0
gui_key_code:  dd 0
