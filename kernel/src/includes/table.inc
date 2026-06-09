; =============================================================================
; table.inc — Swedish (se) keymap, indexed by PS/2 scancode set 1
; =============================================================================
;
; Why this file is a separate module
; ----------------------------------
; The keymap is 1 byte per scancode, so any multi-byte character in the
; source shifts every entry after it. The original keymap_swe in
; input.inc had this bug in two places: `'\\'` assembled as two bytes
; in single quotes, and non-ASCII literals encoded as UTF-8 by NASM.
;
; The US keymap (input.drv) was also affected — fixed by using a single
; backslash. This file avoids both issues by using explicit single bytes.
; Each row is annotated with its scancode so adding or changing an entry
; is a one-line, low-risk edit.
;
; Character codes used
; --------------------
;   0xA4  ¤    currency sign (Swedish shift+4)
;   0xA8  ¨    diaeresis (dead key, unshifted Swedish on some variants)
;   0xB4  ´    acute (dead key, Swedish right-of-backspace unshifted)
;   0xC4  Ä
;   0xC5  Å
;   0xD6  Ö
;   0xE4  ä
;   0xE5  å
;   0xF6  ö
;   0x27  '    apostrophe (NASM 3.01 won't accept '\'' as the escape,
;                  hence the hex literal)
; =============================================================================


section .rodata


; -----------------------------------------------------------------------------
; keymap_se_normal — unshifted Swedish layout
; -----------------------------------------------------------------------------
keymap_se_normal:
    db 0       ; 00
    db 0       ; 01  ESC
    db '1'     ; 02
    db '2'     ; 03
    db '3'     ; 04
    db '4'     ; 05
    db '5'     ; 06
    db '6'     ; 07
    db '7'     ; 08
    db '8'     ; 09
    db '9'     ; 0A
    db '0'     ; 0B
    db '+'     ; 0C
    db 0xB4    ; 0D  ´ (dead acute)
    db 8       ; 0E  Backspace
    db 9       ; 0F  Tab

    db 'q'     ; 10
    db 'w'     ; 11
    db 'e'     ; 12
    db 'r'     ; 13
    db 't'     ; 14
    db 'y'     ; 15
    db 'u'     ; 16
    db 'i'     ; 17
    db 'o'     ; 18
    db 'p'     ; 19
    db 0xE5    ; 1A  å
    db 0xA8    ; 1B  ¨ (dead diaeresis)
    db 13      ; 1C  Enter
    db 0       ; 1D  Ctrl
    db 'a'     ; 1E
    db 's'     ; 1F

    db 'd'     ; 20
    db 'f'     ; 21
    db 'g'     ; 22
    db 'h'     ; 23
    db 'j'     ; 24
    db 'k'     ; 25
    db 'l'     ; 26
    db 0xF6    ; 27  ö
    db 0xE4    ; 28  ä
    db 0x27    ; 29  '
    db 0       ; 2A  LShift
    db '<'     ; 2B
    db 'z'     ; 2C
    db 'x'     ; 2D
    db 'c'     ; 2E
    db 'v'     ; 2F

    db 'b'     ; 30
    db 'n'     ; 31
    db 'm'     ; 32
    db ','     ; 33
    db '.'     ; 34
    db '-'     ; 35
    db 0       ; 36  RShift
    db '*'     ; 37  keypad *
    db 0       ; 38  Alt
    db ' '     ; 39  Space


; -----------------------------------------------------------------------------
; keymap_se_shift — shifted Swedish layout
; -----------------------------------------------------------------------------
keymap_se_shift:
    db 0       ; 00
    db 0       ; 01  ESC
    db '!'     ; 02
    db '"'     ; 03
    db '#'     ; 04
    db 0xA4    ; 05  ¤
    db '%'     ; 06
    db '&'     ; 07
    db '/'     ; 08
    db '('     ; 09
    db ')'     ; 0A
    db '='     ; 0B
    db '?'     ; 0C
    db 0x60    ; 0D  ` (dead grave)
    db 8       ; 0E
    db 9       ; 0F

    db 'Q'     ; 10
    db 'W'     ; 11
    db 'E'     ; 12
    db 'R'     ; 13
    db 'T'     ; 14
    db 'Y'     ; 15
    db 'U'     ; 16
    db 'I'     ; 17
    db 'O'     ; 18
    db 'P'     ; 19
    db 0xC5    ; 1A  Å
    db '^'     ; 1B  (some Swedish variants: dead diaeresis, '^' is common)
    db 13      ; 1C
    db 0       ; 1D
    db 'A'     ; 1E
    db 'S'     ; 1F

    db 'D'     ; 20
    db 'F'     ; 21
    db 'G'     ; 22
    db 'H'     ; 23
    db 'J'     ; 24
    db 'K'     ; 25
    db 'L'     ; 26
    db 0xD6    ; 27  Ö
    db 0xC4    ; 28  Ä
    db '*'     ; 29
    db 0       ; 2A
    db '>'     ; 2B
    db 'Z'     ; 2C
    db 'X'     ; 2D
    db 'C'     ; 2E
    db 'V'     ; 2F

    db 'B'     ; 30
    db 'N'     ; 31
    db 'M'     ; 32
    db ';'     ; 33
    db ':'     ; 34
    db '_'     ; 35
    db 0       ; 36
    db '*'     ; 37
    db 0       ; 38
    db ' '     ; 39
