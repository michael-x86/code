; =============================================================================
; gdemo — VGA Mode 13h graphics engine demo
; =============================================================================
; Switches to 320x200x256 graphics, draws shapes + text, and tracks the
; mouse cursor. Press any key to return to the text shell.
; =============================================================================

[bits 32]
[org 0x00000000]

%include "userland.inc"

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

; 3-3-2 palette colors (index encodes RRRGGGBB)
%define COL_BLACK   0x00
%define COL_BLUE    0x03
%define COL_GREEN   0x1C
%define COL_RED     0xE0
%define COL_YELLOW  0xFC
%define COL_WHITE   0xFF

_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

    mov eax, SYS_GFX_ENTER
    int 0x80

.loop:
    ; Throttle to the 100 Hz tick so we don't spin redrawing.
    mov eax, SYS_TICK
    int 0x80
    mov [ebp + last_tick], eax
.wait:
    mov eax, SYS_TICK
    int 0x80
    cmp eax, [ebp + last_tick]
    je .wait

    ; Background.
    mov eax, SYS_GFX_CLEAR
    mov ebx, COL_BLUE
    int 0x80

    ; Filled rectangle (yellow).
    mov eax, SYS_GFX_FILLRECT
    mov ebx, 20
    mov ecx, 20
    mov edx, COL_YELLOW
    mov esi, 80
    mov edi, 50
    int 0x80

    ; Outlined rectangle (red).
    mov eax, SYS_GFX_RECT
    mov ebx, 130
    mov ecx, 20
    mov edx, COL_RED
    mov esi, 80
    mov edi, 50
    int 0x80

    ; Diagonal line (white).
    mov eax, SYS_GFX_LINE
    mov ebx, 10
    mov ecx, 150
    mov edx, 300
    mov esi, 190
    mov edi, COL_WHITE
    int 0x80

    ; Title text (white).
    mov eax, SYS_GFX_STRING
    lea esi, [ebp + msg]
    mov ebx, 70
    mov ecx, 95
    mov edx, COL_WHITE
    int 0x80

    ; Read mouse and draw a small cursor block (green).
    mov eax, SYS_MOUSE
    lea edi, [ebp + mbuf]
    int 0x80
    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + mbuf]       ; mouse x
    mov ecx, [ebp + mbuf + 4]   ; mouse y
    mov edx, COL_GREEN
    mov esi, 4
    mov edi, 4
    int 0x80

    ; Present the frame.
    mov eax, SYS_GFX_BLIT
    int 0x80

    ; Exit on any key.
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz .loop

    mov eax, SYS_GFX_EXIT
    int 0x80
    ret

msg        db "MODE 13H GRAPHICS - PRESS A KEY", 0
last_tick  dd 0
mbuf       dd 0, 0, 0
