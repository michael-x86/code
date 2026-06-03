; ============================================================================
; enigma_main.asm — Entry point and main flow
; ============================================================================
; Flow:
;   1. Print banner
;   2. Prompt for date
;   3. Parse YYYYMMDD
;   4. Initialize hash, derive daily key
;   5. Display daily key settings
;   6. Reset to Grundstellung, encrypt message key (8-char indicator)
;   7. Display indicator
;   8. Apply message key as new rotor positions
;   9. Enter interactive encrypt/decrypt loop
; ============================================================================

section .data

banner:
    db "===========================================", 0x0A
    db "  ENIGMA M4 — 32-bit x86 Assembly", 0x0A
    db "===========================================", 0x0A
    db 0x0A, 0
banner_len equ $ - banner - 1

msg_daily_key:
    db "Daily key (derived from date):", 0x0A, 0
msg_daily_key_len equ $ - msg_daily_key - 1

msg_rotors:     db "  Rotors (slow-mid-fast-thin): ", 0
msg_rotors_len  equ $ - msg_rotors - 1

msg_reflector:  db "  Reflector:                    ", 0
msg_reflector_len equ $ - msg_reflector - 1

msg_rings:      db "  Ring settings:                ", 0
msg_rings_len   equ $ - msg_rings - 1

msg_positions:  db "  Grundstellung:                ", 0
msg_positions_len equ $ - msg_positions - 1

msg_plugs:      db "  Plugboard pairs:              ", 0
msg_plugs_len   equ $ - msg_plugs - 1

msg_message_key:
    db "Message key:    ", 0
msg_message_key_len equ $ - msg_message_key - 1

msg_indicator:
    db "Indicator:      ", 0
msg_indicator_len equ $ - msg_indicator - 1

msg_interactive:
    db 0x0A
    db "Type plaintext to encrypt. Empty line to quit.", 0x0A
    db 0
msg_interactive_len equ $ - msg_interactive - 1

space:  db " ", 0
comma:  db ", ", 0
colon:  db ": ", 0
colon_space: db ": ", 0
separator: db " | ", 0

; Rotor name table (just the labels; abbreviations)
; I, II, III, IV, V, VI, VII, VIII, Beta, Gamma
rotor_short:
    db 'I', 0                          ; +0  : "I"     (2 bytes)
rotor_short_2:
    db 'I', 'I', 0                     ; +2  : "II"    (3 bytes)
rotor_short_3:
    db 'I', 'I', 'I', 0                ; +5  : "III"   (4 bytes)
rotor_short_4:
    db 'I', 'V', 0                     ; +9  : "IV"    (3 bytes)
rotor_short_5:
    db 'V', 0                          ; +12 : "V"     (2 bytes)
rotor_short_6:
    db 'V', 'I', 0                     ; +14 : "VI"    (3 bytes)
rotor_short_7:
    db 'V', 'I', 'I', 0                ; +17 : "VII"   (4 bytes)
rotor_short_8:
    db 'V', 'I', 'I', 'I', 0           ; +21 : "VIII"  (5 bytes)
rotor_short_b:
    db 'B', 'e', 't', 'a', 0           ; +26 : "Beta"  (5 bytes)
rotor_short_g:
    db 'G', 'a', 'm', 'm', 'a', 0      ; +31 : "Gamma" (6 bytes)

; Offsets (in bytes from start of rotor_short) for each rotor index 0-9
rotor_offsets:
    dd 0, 2, 5, 9, 12, 14, 17, 21, 26, 31

section .bss

input_buf:  resb 16            ; "YYYYMMDD\n\0"
date_val:   resd 1
char_in:    resb 1
char_out:   resb 1
indicator:  resb 8             ; encrypted message key (8 chars)
indicator_len: equ 8

section .text

global _start

; External references
extern parse_date
extern hash_init
extern hash_expand
extern derive_daily_key
extern derive_message_key
extern apply_message_key
extern enigma_init
extern enigma_reset
extern enigma_step
extern enigma_crypt

extern io_print
extern io_print_n
extern io_readline
extern io_print_int
extern io_print_letter
extern io_newline
extern io_prompt_date
extern io_prompt_msg

extern daily_rotor_idx
extern daily_ring
extern daily_position
extern daily_reflector
extern daily_plugs
extern message_key

; Linux syscalls
%define SYS_WRITE 4
%define SYS_READ  3
%define SYS_EXIT  1
%define STDOUT    1
%define STDIN     0

; ============================================================================
; _start — program entry
; ============================================================================
_start:
    ; Print banner
    mov     esi, banner
    call    io_print

    ; Prompt for date
    call    io_prompt_date

    ; Read date string
    lea     esi, [input_buf]
    mov     ecx, 16
    call    io_readline

    ; Parse date
    lea     esi, [input_buf]
    mov     ecx, 8
    call    parse_date
    mov     [date_val], eax

    ; Initialize hash
    mov     eax, [date_val]
    call    hash_init
    call    hash_expand

    ; Derive daily key
    call    derive_daily_key
    call    derive_message_key

    ; Initialize Enigma state with daily key
    call    enigma_init
    call    enigma_reset         ; ensure starting at Grundstellung

    ; Display daily key
    call    display_daily_key

    ; --- Encrypt the message key as an 8-character indicator ---
    ; The Kriegsmarine procedure: operator picks 4-letter key, encrypts it
    ; twice using the daily Grundstellung. We display the encrypted indicator.
    call    encrypt_message_indicator
    call    display_indicator

    ; Display the (plaintext) message key for verification
    call    display_message_key

    ; Now set the rotor positions to the message key
    call    apply_message_key
    call    enigma_reset

    ; Print interactive banner
    lea     esi, [msg_interactive]
    call    io_print

    ; Enter interactive loop
    call    interactive_loop

    ; Exit
    mov     eax, SYS_EXIT
    xor     ebx, ebx
    int     0x80

; ============================================================================
; display_daily_key — print the derived settings
; ============================================================================
display_daily_key:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    esi

    lea     esi, [msg_daily_key]
    call    io_print

    ; --- Rotors ---
    lea     esi, [msg_rotors]
    call    io_print
    ; Print 4 rotor names separated by "-"
    xor     ecx, ecx
.disp_rotor_loop:
    cmp     ecx, 4
    jge     .disp_rotors_done
    movzx   eax, byte [daily_rotor_idx + ecx]
    ; Lookup name in rotor_offsets
    push    ecx
    mov     ebx, eax
    shl     ebx, 2
    mov     esi, [rotor_offsets + ebx]
    add     esi, rotor_short
    call    io_print
    pop     ecx
    cmp     ecx, 3
    je      .skip_sep
    lea     esi, [space]
    lea     esi, [separator]
    call    io_print
.skip_sep:
    inc     ecx
    jmp     .disp_rotor_loop
.disp_rotors_done:
    call    io_newline

    ; --- Reflector ---
    lea     esi, [msg_reflector]
    call    io_print
    movzx   eax, byte [daily_reflector]
    cmp     eax, 0
    je      .refl_b
    ; C
    mov     al, 'C'
    jmp     .refl_out
.refl_b:
    mov     al, 'B'
.refl_out:
    mov     [char_out], al
    lea     esi, [char_out]
    call    io_print_n
    mov     al, 0
    mov     [char_out], al
    call    io_newline

    ; --- Ring settings ---
    lea     esi, [msg_rings]
    call    io_print
    xor     ecx, ecx
.disp_ring:
    cmp     ecx, 4
    jge     .ring_done
    movzx   eax, byte [daily_ring + ecx]
    call    io_print_letter
    cmp     ecx, 3
    je      .ring_skip
    lea     esi, [space]
    call    io_print
.ring_skip:
    inc     ecx
    jmp     .disp_ring
.ring_done:
    call    io_newline

    ; --- Grundstellung ---
    lea     esi, [msg_positions]
    call    io_print
    xor     ecx, ecx
.disp_pos:
    cmp     ecx, 4
    jge     .pos_done
    movzx   eax, byte [daily_position + ecx]
    call    io_print_letter
    cmp     ecx, 3
    je      .pos_skip
    lea     esi, [space]
    call    io_print
.pos_skip:
    inc     ecx
    jmp     .disp_pos
.pos_done:
    call    io_newline

    ; --- Plugboard pairs ---
    lea     esi, [msg_plugs]
    call    io_print
    call    display_plugboard
    call    io_newline

    pop     esi
    pop     ebx
    pop     eax
    pop     ebp
    ret

; ============================================================================
; display_plugboard — print all 10 plugboard pairs
; ============================================================================
display_plugboard:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx
    push    esi

    xor     ecx, ecx            ; first letter
    xor     ebx, ebx            ; pair count
.dp_loop:
    cmp     ecx, 26
    jge     .dp_done
    movzx   eax, byte [daily_plugs + ecx]
    cmp     eax, ecx
    je      .dp_next             ; unswapped
    cmp     eax, ecx
    jl      .dp_next             ; already printed (pair partner is lower)
    ; Print pair
    mov     al, cl
    call    io_print_letter
    push    ecx
    movzx   eax, byte [daily_plugs + ecx]
    call    io_print_letter
    pop     ecx
    inc     ebx
    cmp     ebx, 10
    jge     .dp_done
    lea     esi, [space]
    call    io_print
.dp_next:
    inc     ecx
    jmp     .dp_loop
.dp_done:
    pop     esi
    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
    ret

; ============================================================================
; encrypt_message_indicator — encrypt the 4-letter message key twice
; using the current (Grundstellung) settings
; Output stored in indicator[8]
; ============================================================================
encrypt_message_indicator:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx

    ; Make sure we're at Grundstellung
    call    enigma_reset

    ; Encrypt message_key 4 letters → indicator[0..3]
    xor     ecx, ecx
.enc1_loop:
    cmp     ecx, 4
    jge     .enc1_done
    call    enigma_step
    movzx   eax, byte [message_key + ecx]
    push    ecx
    call    enigma_crypt
    pop     ecx
    mov     [indicator + ecx], al
    inc     ecx
    jmp     .enc1_loop
.enc1_done:

    ; Reset again and encrypt a second time
    call    enigma_reset

    xor     ecx, ecx
.enc2_loop:
    cmp     ecx, 4
    jge     .enc2_done
    call    enigma_step
    movzx   eax, byte [message_key + ecx]
    push    ecx
    call    enigma_crypt
    pop     ecx
    mov     [indicator + 4 + ecx], al
    inc     ecx
    jmp     .enc2_loop
.enc2_done:

    ; Reset back to Grundstellung for the next phase
    call    enigma_reset

    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
    ret

; ============================================================================
; display_indicator — print 8-char encrypted indicator
; ============================================================================
display_indicator:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ecx

    lea     esi, [msg_indicator]
    call    io_print

    xor     ecx, ecx
.di_loop:
    cmp     ecx, 8
    jge     .di_done
    movzx   eax, byte [indicator + ecx]
    call    io_print_letter
    inc     ecx
    jmp     .di_loop
.di_done:
    call    io_newline

    pop     ecx
    pop     eax
    pop     ebp
    ret

; ============================================================================
; display_message_key — print 4-letter plaintext message key
; ============================================================================
display_message_key:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ecx

    lea     esi, [msg_message_key]
    call    io_print

    xor     ecx, ecx
.dmk_loop:
    cmp     ecx, 4
    jge     .dmk_done
    movzx   eax, byte [message_key + ecx]
    call    io_print_letter
    inc     ecx
    jmp     .dmk_loop
.dmk_done:
    call    io_newline

    pop     ecx
    pop     eax
    pop     ebp
    ret

; ============================================================================
; interactive_loop — read chars, encrypt, print, until empty line
; ============================================================================
interactive_loop:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi

    ; Print initial prompt
    call    io_prompt_msg

.interactive_loop:
    ; Read one byte
    mov     eax, SYS_READ
    mov     ebx, STDIN
    lea     ecx, [char_in]
    mov     edx, 1
    int     0x80
    cmp     eax, 0
    jle     .interactive_done

    movzx   eax, byte [char_in]

    ; Check for newline / EOF
    cmp     al, 0x0A
    je      .check_eof
    cmp     al, 0x0D
    je      .check_eof

    ; Check for letter A-Z
    cmp     al, 'A'
    jl      .interactive_loop
    cmp     al, 'Z'
    jg      .try_lower
    sub     al, 'A'
    jmp     .got_letter

.try_lower:
    cmp     al, 'a'
    jl      .interactive_loop
    cmp     al, 'z'
    jg      .interactive_loop
    sub     al, 'a'             ; lowercase also acceptable

.got_letter:
    ; Step rotors and encrypt
    push    eax
    call    enigma_step
    pop     eax
    push    eax
    call    enigma_crypt
    pop     eax
    add     al, 'A'
    mov     [char_out], al

    ; Print result
    mov     eax, SYS_WRITE
    mov     ebx, STDOUT
    lea     ecx, [char_out]
    mov     edx, 1
    int     0x80

    jmp     .interactive_loop

.check_eof:
    ; If the next read returns 0, we're done. Otherwise, this was a real
    ; newline → print newline and continue.
    mov     eax, SYS_READ
    mov     ebx, STDIN
    lea     ecx, [char_in]
    mov     edx, 1
    mov     edx, 1
    int     0x80
    test    eax, eax
    jle     .interactive_done
    ; It was a real newline, print it
    call    io_newline
    call    io_prompt_msg
    jmp     .interactive_loop

.interactive_done:
    call    io_newline

    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
    ret
