; =============================================================================
; enigma — Kriegsmarine M4 (4-rotor Enigma) simulator for the OS
; =============================================================================
; Ported from kernel/src/enigma/ — a Linux ELF bundle of 6 NASM files using
; a xorshift PRNG to derive daily settings from a YYYYMMDD date. Bundled
; here as a single flat binary that uses the OS's int 0x80 syscall ABI.
;
; Usage:
;   enigma                      — interactive mode (prompts for date + input)
;   enigma e YYYYMMDD MESSAGE   — encrypt MESSAGE using date-derived key
;   enigma d YYYYMMDD MESSAGE   — decrypt MESSAGE (Enigma is symmetric)
;
; All memory accesses use [default rel] addressing, so every `[label]` is
; resolved as the load-address + label-offset, which equals [ebp + label]
; because ebp is set to the address of _start in the loader.
; =============================================================================
;
; =============================================================================
; TABLE OF CONTENTS
; =============================================================================
; 1. SYSTEM CALL NUMBERS ............... (~line 22)
; 2. ENIGMA CONSTANTS ................. (~line 33)
; 3. ENTRY POINT & CLI PARSING ....... (~line 46)
;    3.1 Non-interactive mode (CLI args)
;    3.2 Interactive mode entry
; 4. INPUT/OUTPUT UTILITIES ........... (~line 277)
;    4.1 blocking_get_key
;    4.2 asc_to_int
;    4.3 io_print / io_print_letter / io_newline
; 5. DISPLAY ROUTINES ................ (~line 380)
;    5.1 display_daily_key
;    5.2 display_plugboard
;    5.3 encrypt_message_indicator
;    5.4 display_indicator
; 6. INTERACTIVE MODE ................. (~line 625)
;    6.1 interactive_loop (main input loop)
;    6.2 Help system (.show_help)
; 7. ENIGMA CORE ROUTINES ............ (~line 771)
;    7.1 mod26 (modular arithmetic)
;    7.2 is_at_notch (rotor notch check)
;    7.3 enigma_init (initialize Enigma state)
;    7.4 enigma_reset (reset rotor positions)
;    7.5 enigma_step (advance rotors)
;    7.6 enigma_crypt (encrypt/decrypt letter)
; 8. KEY DERIVATION .................. (~line 993)
;    8.1 hash_init (initialize PRNG)
;    8.2 xorshift32 (PRNG algorithm)
;    8.3 hash_expand (expand hash to key material)
;    8.4 derive_daily_key (derive daily settings)
;    8.5 derive_plugboard (derive plugboard pairs)
;    8.6 derive_message_key (derive 4-letter message key)
;    8.7 apply_message_key (apply to rotor positions)
; 9. RODATA SECTION .................. (~line 1432)
;    9.1 Strings and messages
;    9.2 Rotor wiring tables (10 rotors)
;    9.3 Rotor notch positions
;    9.4 Reflector wiring (B and C)
;    9.5 Rotor names
; 10. MUTABLE STATE ................. (~line 1505)
;    10.1 Input/output buffers
;    10.2 Derivation state
;    10.3 Enigma working state
;    10.4 Plugboard state
; =============================================================================
;
; =============================================================================
; 1. SYSTEM CALL NUMBERS (duplicated from userland.inc for clarity)
; =============================================================================
[bits 32]
[org 0x00000000]

%include "userland.inc"

; =============================================================================
; 1. SYSTEM CALL NUMBERS (duplicated from userland.inc for clarity)
; =============================================================================
%define SYS_PUTCHAR   0       ; ebx = character to print
%define SYS_PRINT     1       ; esi = NUL-terminated string
%define SYS_PRINT_CR  2       ; esi = string (auto CR before)
%define SYS_NEWLINE   3       ; Print newline (CR+LF)
%define SYS_CLS       4       ; Clear screen
%define SYS_GETKEY    7       ; Block until key, al = character
%define SYS_GET_ARG   14      ; ebx = arg num, edi = buffer

; =============================================================================
; 2. ENIGMA CONSTANTS
; =============================================================================
%define ALPHA         26      ; Letters in alphabet
%define ALPHA2        52      ; ALPHA * 2 (for modular arithmetic)

; Derived constants for code clarity
%define NUM_ROTORS    4       ; Number of rotors in M4 (3 main + 1 thin)
%define NUM_MAIN      8       ; Main rotor choices (I-VIII)
%define NUM_THIN      2       ; Thin rotor choices (Beta, Gamma)
%define NUM_REFLECT   2       ; Reflector choices (B, C)
%define MAX_PLUGS     10      ; Maximum plugboard pairs

; =============================================================================
; 3. ENTRY POINT & CLI PARSING
; =============================================================================
global _start
_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

; -----------------------------------------------------------------------------
; 3.1 Non-interactive mode (CLI args)
; -----------------------------------------------------------------------------
    ; --- Try to read CLI arguments ---
    ; arg 1: mode ('e' or 'd')
    lea edi, [ebp + arg_buf_1]
    mov ebx, 1
    mov eax, SYS_GET_ARG
    int 0x80
    cmp eax, -1
    je .interactive

    ; arg 2: date (YYYYMMDD)
    lea edi, [ebp + arg_buf_2]
    mov ebx, 2
    mov eax, SYS_GET_ARG
    int 0x80
    cmp eax, -1
    je .interactive

    ; arg 3: message
    lea edi, [ebp + arg_buf_3]
    mov ebx, 3
    mov eax, SYS_GET_ARG
    int 0x80
    cmp eax, -1
    je .interactive

    ; We have all 3 CLI args — non-interactive mode
    ; Parse date
    lea esi, [ebp + arg_buf_2]
    call asc_to_int
    mov [ebp + date_val], eax

    ; Derive daily key from date
    mov eax, [ebp + date_val]
    call hash_init
    call hash_expand
    call derive_daily_key
    call derive_message_key
    call enigma_init

    ; Apply message key as daily position
    call apply_message_key
    call enigma_reset

    ; Process each character of the message
    lea esi, [ebp + arg_buf_3]
    xor ecx, ecx
.process_msg:
    movzx eax, byte [esi + ecx]
    test al, al
    jz .process_done

    ; Check for space - encode as 'X' (23) in cleartext
    cmp al, ' '
    jne .not_space
    mov al, 23          ; 'X' - 'A' = 23
    jmp .valid_letter
.not_space:

    ; Convert to uppercase
    cmp al, 'a'
    jl .check_upper
    cmp al, 'z'
    jg .check_upper
    sub al, 32              ; lowercase -> uppercase

.check_upper:
    cmp al, 'A'
    jl .skip_char
    cmp al, 'Z'
    jg .skip_char

.valid_letter:
    ; Valid letter
    sub al, 'A'
    push eax
    push ecx
    push esi
    call enigma_step
    pop esi
    pop ecx
    pop eax
    call enigma_crypt
    add al, 'A'
    mov ebx, eax
    mov eax, SYS_PUTCHAR
    int 0x80
    jmp .next_char

.skip_char:
    ; Output non-letter chars verbatim
    push ecx
    push esi
    mov ebx, eax
    mov eax, SYS_PUTCHAR
    int 0x80
    pop esi
    pop ecx

.next_char:
    inc ecx
    jmp .process_msg

.process_done:
    mov eax, SYS_NEWLINE
    int 0x80
    ret

; =============================================================================
; 3.2 Interactive mode entry
; =============================================================================
.interactive:
    ; DEBUG: Print 'I' to confirm we entered interactive mode
    ;mov ebx, 'I'
    ;mov eax, SYS_PUTCHAR
    ;int 0x80

    mov eax, SYS_CLS
    int 0x80

    lea esi, [ebp + banner]
    mov eax, SYS_PRINT
    int 0x80

    ; DEBUG: Print 'P' to confirm we printed banner
    ;mov ebx, 'P'
    ;mov eax, SYS_PUTCHAR
    ;int 0x80

    lea esi, [ebp + prompt_date]
    mov eax, SYS_PRINT_CR
    int 0x80

    ; Read date string one byte at a time
    lea edi, [ebp + input_buf]
    xor ecx, ecx
.read_date:
    call blocking_get_key
    test ecx, ecx
    jnz .have_chars
    ; No chars yet — ignore everything except digit and backspace. The
    ; shell may have left a stray CR / LF in the buffer from launching us.
    test al, al
    jz .read_date
    cmp al, '0'
    jb .read_date
    cmp al, '9'
    ja .try_bs
    jmp .got_digit
.try_bs:
    cmp al, 0x08
    jne .read_date
    jmp .date_bs
.have_chars:
    cmp al, 0x0D
    je .date_done
    cmp al, 0x0A
    je .date_done
    test al, al
    jz .read_date
    cmp al, '0'
    jb .read_date
    cmp al, '9'
    ja .not_digit
.got_digit:
    cmp ecx, 8
    jge .read_date
    mov [edi + ecx], al
    inc ecx
    mov ebx, eax
    mov eax, SYS_PUTCHAR
    int 0x80
    jmp .read_date
.not_digit:
    cmp al, 0x08
    jne .read_date
    jmp .date_bs
.date_bs:
    test ecx, ecx
    jz .read_date
    dec ecx
    mov ebx, 0x08
    mov eax, SYS_PUTCHAR
    int 0x80
    mov ebx, ' '
    mov eax, SYS_PUTCHAR
    int 0x80
    mov ebx, 0x08
    mov eax, SYS_PUTCHAR
    int 0x80
    jmp .read_date
.date_done:
    mov byte [edi + ecx], 0
    mov eax, SYS_NEWLINE
    int 0x80
    test ecx, ecx
    jz .exit

    lea esi, [ebp + input_buf]
    call asc_to_int
    mov [ebp + date_val], eax
    
    mov eax, [ebp + date_val]
    call hash_init
    call hash_expand

    call derive_daily_key
    call derive_message_key

    call enigma_init
    call enigma_reset

    call display_daily_key

    call encrypt_message_indicator
    call display_indicator
    call display_message_key

    call apply_message_key
    call enigma_reset

    lea esi, [ebp + msg_interactive]
    mov eax, SYS_PRINT_CR
    int 0x80

    call interactive_loop

.exit:
    ret

; =============================================================================
; 4.1 blocking_get_key — Busy-loop until a non-zero key arrives
; =============================================================================
; Input:  None
; Output: AL = ASCII character (non-zero)
; Clobbers: EAX
; =============================================================================
blocking_get_key:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz blocking_get_key
    ret

; =============================================================================
; 4.2 asc_to_int — NUL-terminated ASCII decimal string → 32-bit integer
; =============================================================================
; Input:  ESI = pointer to NUL-terminated decimal string
; Output: EAX = 32-bit integer value
; Clobbers: EAX, EBX, ECX, EDX, ESI
; =============================================================================
asc_to_int:
    push ebx
    push ecx
    push edx
    push esi
    xor eax, eax
    xor ecx, ecx
.loop:
    movzx ebx, byte [esi + ecx]
    test bl, bl
    jz .done
    cmp bl, '0'
    jb .done
    cmp bl, '9'
    ja .done
    imul eax, 10
    mov edx, ebx
    sub edx, '0'
    add eax, edx
    inc ecx
    jmp .loop
.done:
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; =============================================================================
; 4.3 io_print / io_print_letter / io_newline
; =============================================================================
; ---
; io_print — Print NUL-terminated string at ESI
; -----------------------------------------------------------------------------
; Input:  ESI = pointer to NUL-terminated string
; Output: None
; Clobbers: EAX, ECX, EDX (via syscall)
; =============================================================================
io_print:
    push esi
    push ecx
    push edx
    mov edx, esi
.len:
    cmp byte [edx], 0
    je .go
    inc edx
    jmp .len
.go:
    sub edx, esi
    mov ecx, esi
    mov eax, SYS_PRINT
    int 0x80
    pop edx
    pop ecx
    pop esi
    ret

; -----------------------------------------------------------------------------
; io_print_letter — Print one A-Z letter from index
; -----------------------------------------------------------------------------
; Input:  AL = letter index (0-25)
; Output: None (prints character)
; Clobbers: EAX, EBX
; =============================================================================
io_print_letter:
    add al, 'A'
    mov [ebp + letter_buf], al
    mov ebx, eax
    mov eax, SYS_PUTCHAR
    int 0x80
    ret

; -----------------------------------------------------------------------------
; io_newline
; -----------------------------------------------------------------------------
; Input:  None
; Output: None
; Clobbers: EAX
; =============================================================================
io_newline:
    mov eax, SYS_NEWLINE
    int 0x80
    ret

; =============================================================================
; 5.1 display_daily_key — print derived settings
; =============================================================================
; Input:  daily_rotor_idx, daily_ring, daily_position, daily_reflector,
;         daily_plugs must be set
; Output: None (prints to screen)
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI
; =============================================================================
display_daily_key:
    push eax
    push ebx
    push ecx
    push edx
    push esi

    lea esi, [ebp + msg_daily_key]
    call io_print
    call io_newline

    ; Rotors
    lea esi, [ebp + msg_rotors]
    call io_print
    xor ecx, ecx
.disp_rotor_loop:
    cmp ecx, 4
    jge .disp_rotors_done
    movzx eax, byte [ebp + daily_rotor_idx + ecx]
    push ecx
    mov ebx, eax
    shl ebx, 1
    movzx esi, word [ebp + rotor_name_offsets + ebx]
    lea esi, [ebp + esi + rotor_names]
    call io_print
    pop ecx
    cmp ecx, 3
    je .skip_sep
    lea esi, [ebp + separator]
    call io_print
.skip_sep:
    inc ecx
    jmp .disp_rotor_loop
.disp_rotors_done:
    call io_newline

    ; Reflector
    lea esi, [ebp + msg_reflector]
    call io_print
    movzx eax, byte [ebp + daily_reflector]
    inc eax
    call io_print_letter
    call io_newline

    ; Ring settings
    lea esi, [ebp + msg_rings]
    call io_print
    xor ecx, ecx
.disp_ring:
    cmp ecx, 4
    jge .ring_done
    movzx eax, byte [ebp + daily_ring + ecx]
    call io_print_letter
    cmp ecx, 3
    je .ring_skip
    lea esi, [ebp + space_str]
    call io_print
.ring_skip:
    inc ecx
    jmp .disp_ring
.ring_done:
    call io_newline

    ; Grundstellung
    lea esi, [ebp + msg_positions]
    call io_print
    xor ecx, ecx
.disp_pos:
    cmp ecx, 4
    jge .pos_done
    movzx eax, byte [ebp + daily_position + ecx]
    call io_print_letter
    cmp ecx, 3
    je .pos_skip
    lea esi, [ebp + space_str]
    call io_print
.pos_skip:
    inc ecx
    jmp .disp_pos
.pos_done:
    call io_newline

    ; Plugboard pairs
    lea esi, [ebp + msg_plugs]
    call io_print
    call display_plugboard
    call io_newline

    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; =============================================================================
; 5.2 display_plugboard — print all 10 plugboard pairs
; =============================================================================
; Input:  daily_plugs must be set
; Output: None (prints to screen)
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI
; =============================================================================
display_plugboard:
    push eax
    push ebx
    push ecx
    push edx
    push esi

    xor ecx, ecx
    xor ebx, ebx
.dp_loop:
    cmp ecx, 26
    jge .dp_done
    movzx eax, byte [ebp + daily_plugs + ecx]
    cmp eax, ecx
    je .dp_next
    cmp eax, ecx
    jl .dp_next
    mov al, cl
    call io_print_letter
    push ecx
    movzx eax, byte [ebp + daily_plugs + ecx]
    call io_print_letter
    pop ecx
    inc ebx
    cmp ebx, 10
    jge .dp_done
    lea esi, [ebp + space_str]
    call io_print
.dp_next:
    inc ecx
    jmp .dp_loop
.dp_done:
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; =============================================================================
; 5.3 encrypt_message_indicator — encrypt the 4-letter message key twice
; =============================================================================
; Input:  message_key must be set, Enigma initialized
; Output: indicator contains 8-letter doubled key
; Clobbers: EAX, ECX
; =============================================================================
encrypt_message_indicator:
    push eax
    push ecx

    call enigma_reset
    xor ecx, ecx
.enc1_loop:
    cmp ecx, 4
    jge .enc1_done
    call enigma_step
    movzx eax, byte [ebp + message_key + ecx]
    push ecx
    call enigma_crypt
    pop ecx
    mov [ebp + indicator + ecx], al
    inc ecx
    jmp .enc1_loop
.enc1_done:
    call enigma_reset
    xor ecx, ecx
.enc2_loop:
    cmp ecx, 4
    jge .enc2_done
    call enigma_step
    movzx eax, byte [ebp + message_key + ecx]
    push ecx
    call enigma_crypt
    pop ecx
    mov [ebp + indicator + 4 + ecx], al
    inc ecx
    jmp .enc2_loop
.enc2_done:
    call enigma_reset
    pop ecx
    pop eax
    ret

; =============================================================================
; 5.4 display_indicator
; =============================================================================
; Input:  indicator must be set (8 bytes)
; Output: None (prints to screen)
; Clobbers: EAX, ECX, ESI
; =============================================================================
display_indicator:
    push eax
    push ecx
    lea esi, [ebp + msg_indicator]
    call io_print
    xor ecx, ecx
.di_loop:
    cmp ecx, 8
    jge .di_done
    movzx eax, byte [ebp + indicator + ecx]
    call io_print_letter
    inc ecx
    jmp .di_loop
.di_done:
    call io_newline
    pop ecx
    pop eax
    ret

; =============================================================================
; display_message_key
; =============================================================================
; Input:  message_key must be set (4 bytes)
; Output: None (prints to screen)
; Clobbers: EAX, ECX, ESI
; =============================================================================
display_message_key:
    push eax
    push ecx
    lea esi, [ebp + msg_message_key]
    call io_print
    xor ecx, ecx
.dmk_loop:
    cmp ecx, 4
    jge .dmk_done
    movzx eax, byte [ebp + message_key + ecx]
    call io_print_letter
    inc ecx
    jmp .dmk_loop
.dmk_done:
    call io_newline
    pop ecx
    pop eax
    ret

; =============================================================================
; 6. INTERACTIVE MODE
; =============================================================================
; ---
; 6.1 interactive_loop — read chars, encrypt, print, until empty line
; -----------------------------------------------------------------------------
; Input:  Enigma must be initialized, message key applied
; Output: None (prints encrypted characters)
; Clobbers: EAX, ECX, ESI
; =============================================================================
interactive_loop:
    push eax
    push ecx
    push esi

    lea esi, [ebp + prompt_msg]
    call io_print

.interactive_loop:
    call blocking_get_key
    movzx eax, al
    
    ; Check for help command ('?' or 'h'/'H')
    cmp al, '?'
    je .show_help
    ;cmp al, 'h'
    ;je .show_help
    ;cmp al, 'H'
    ;je .show_help
    
    cmp al, 0x0A
    je .newline_or_eof
    cmp al, 0x0D
    je .newline_or_eof

    ; Check for space - encode as 'X' (23) in cleartext
    cmp al, ' '
    jne .not_space
    mov al, 23          ; 'X' - 'A' = 23
    jmp .got_letter
.not_space:

    cmp al, 'A'
    jl .interactive_loop
    cmp al, 'Z'
    jg .try_lower
    sub al, 'A'
    jmp .got_letter
.try_lower:
    cmp al, 'a'
    jl .interactive_loop
    cmp al, 'z'
    jg .interactive_loop
    sub al, 'a'

.got_letter:
    push eax
    call enigma_step
    pop eax
    call enigma_crypt
    add al, 'A'
    mov ebx, eax
    mov eax, SYS_PUTCHAR
    int 0x80
    jmp .interactive_loop
    
; -----------------------------------------------------------------------------
; 6.2 Help system (.show_help)
; -----------------------------------------------------------------------------
.show_help:
    lea esi, [ebp + msg_help]
    call io_print
    lea esi, [ebp + msg_help1]
    call io_print
    lea esi, [ebp + msg_help2]
    call io_print
    lea esi, [ebp + msg_help3]
    call io_print
    lea esi, [ebp + msg_help4]
    call io_print
    lea esi, [ebp + msg_help5]
    call io_print
    lea esi, [ebp + msg_help6]
    call io_print
    lea esi, [ebp + msg_help7]
    call io_print
    lea esi, [ebp + msg_help8]
    call io_print
    lea esi, [ebp + msg_help9]
    call io_print
    lea esi, [ebp + msg_help10]
    call io_print
    lea esi, [ebp + msg_help11]
    call io_print
    lea esi, [ebp + msg_help12]
    call io_print
    lea esi, [ebp + msg_help13]
    call io_print
    lea esi, [ebp + msg_help14]
    call io_print
    lea esi, [ebp + msg_help15]
    call io_print
    lea esi, [ebp + msg_help16]
    call io_print
    lea esi, [ebp + msg_help17]
    call io_print
    lea esi, [ebp + msg_help18]
    call io_print
    lea esi, [ebp + msg_help19]
    call io_print
    lea esi, [ebp + msg_help20]
    call io_print
    call io_newline
    jmp .interactive_loop
    
.newline_or_eof:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jnz .had_real_newline
    jmp .interactive_done

.had_real_newline:
    call io_newline
    lea esi, [ebp + prompt_msg]
    call io_print
    jmp .interactive_loop

.interactive_done:
    call io_newline
    pop esi
    pop ecx
    pop eax
    ret

; =============================================================================
; 7. ENIGMA CORE ROUTINES
; =============================================================================
; ---
; 7.1 mod26 — Calculate EAX mod 26
; -----------------------------------------------------------------------------
; Input:  EAX = value to reduce
; Output: EAX = EAX mod 26 (range 0-25)
; Clobbers: EDX
; =============================================================================
    mod26:
    push ebx
    xor edx, edx
    mov ebx, ALPHA
    div ebx
    mov eax, edx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; 7.2 is_at_notch — Check if rotor is at notch position
; -----------------------------------------------------------------------------
; Input:  AL = current rotor position (0-25)
;         ECX = number of notches (0, 1, or 2)
;         EDX = pointer to notch position byte(s)
; Output: AL = 1 if at notch, 0 otherwise
; Clobbers: EAX, ECX, EDX
; =============================================================================
is_at_notch:
    push ebx
    test ecx, ecx
    jz .notch_no
.notch_loop:
    cmp al, [edx]
    je .notch_yes
    inc edx
    dec ecx
    jnz .notch_loop
.notch_no:
    xor eax, eax
    pop ebx
    ret
.notch_yes:
    mov al, 1
    pop ebx
    ret

; =============================================================================
; 7.3 enigma_init — copy daily key into working state, precompute reverse wiring
; =============================================================================
; Input:  daily_rotor_idx, daily_ring, daily_position, daily_reflector
;         daily_plugs must be set
; Output: rotor_fwd, rotor_rev, refl_wiring, plug_map populated
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI
; =============================================================================
enigma_init:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    xor ebx, ebx

.init_rotor:
    cmp ebx, 4
    jge .init_refl

    movzx eax, byte [ebp + daily_rotor_idx + ebx]

    ; Forward wiring copy
    imul eax, 26
    lea esi, [ebp + eax + rotor_wiring]
    mov edi, ebx
    imul edi, 26
    lea edi, [ebp + edi + rotor_fwd]
    push ecx
    xor ecx, ecx
.fwd_copy:
    movzx eax, byte [esi + ecx]
    sub eax, 'A'
    mov [edi + ecx], al
    inc ecx
    cmp ecx, 26
    jl .fwd_copy
    pop ecx

    ; Reverse wiring
    movzx eax, byte [ebp + daily_rotor_idx + ebx]
    imul eax, 26
    lea esi, [ebp + eax + rotor_wiring]
    mov edi, ebx
    imul edi, 26
    lea edi, [ebp + edi + rotor_rev]
    xor ecx, ecx
.rev_loop:
    cmp ecx, ALPHA
    jge .rev_done
    movzx eax, byte [esi + ecx]
    sub eax, 'A'
    mov byte [edi + eax], cl
    inc ecx
    jmp .rev_loop
.rev_done:

    movzx eax, byte [ebp + daily_rotor_idx + ebx]
    cmp eax, 8
    jge .no_notch

    push ebx
    mov edi, eax
    cmp eax, 5
    jle .offset_ok
    add edi, eax
    sub edi, 5
.offset_ok:
    movzx ecx, byte [ebp + eax + rotor_notch_count]
    mov [ebp + ebx + rotor_ncnt], cl
    mov eax, ebx
    add eax, eax
    movzx edx, byte [ebp + edi + rotor_notch]
    mov [ebp + eax + rotor_wnotch], dl
    cmp ecx, 2
    jl .notch_copied
    movzx edx, byte [ebp + edi + 1 + rotor_notch]
    mov [ebp + eax + 1 + rotor_wnotch], dl
.notch_copied:
    pop ebx
    jmp .next_rotor

.no_notch:
    mov byte [ebp + ebx + rotor_ncnt], 0

.next_rotor:
    inc ebx
    jmp .init_rotor

.init_refl:
    movzx eax, byte [ebp + daily_reflector]
    imul eax, 26
    lea esi, [ebp + eax + reflector_wiring]
    lea edi, [ebp + refl_wiring]
    push ecx
    xor ecx, ecx
.refl_copy:
    movzx eax, byte [esi + ecx]
    sub eax, 'A'
    mov [edi + ecx], al
    inc ecx
    cmp ecx, 26
    jl .refl_copy
    pop ecx

    lea esi, [ebp + daily_plugs]
    lea edi, [ebp + plug_map]
    mov ecx, 26
    rep movsb

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; =============================================================================
; 7.4 enigma_reset — reset rotor positions to daily_position values
; =============================================================================
; Input:  daily_position must be set
; Output: rotor_pos set to daily_position values
; Clobbers: EAX, ECX, ESI, EDI
; =============================================================================
enigma_reset:
    push esi
    push edi
    push ecx
    lea esi, [ebp + daily_position]
    lea edi, [ebp + rotor_pos]
    mov ecx, 4
    rep movsb
    pop ecx
    pop edi
    pop esi
    ret

; =============================================================================
; 7.5 enigma_step — advance rotors before encoding a character
; =============================================================================
; Input:  rotor_pos, rotor_ncnt, rotor_wnotch must be set
; Output: rotor_pos updated (rotors stepped)
; Clobbers: EAX, ECX, EDX
; =============================================================================
enigma_step:
    push ebx
    push ecx
    push edx

    movzx eax, byte [ebp + rotor_pos + 2]
    inc eax
    call mod26
    mov [ebp + rotor_pos + 2], al

    movzx eax, byte [ebp + rotor_pos + 2]
    movzx ecx, byte [ebp + rotor_ncnt + 2]
    lea edx, [ebp + rotor_wnotch + 4]
    call is_at_notch
    test al, al
    jz .check_mid

    movzx eax, byte [ebp + rotor_pos + 1]
    inc eax
    call mod26
    mov [ebp + rotor_pos + 1], al

.check_mid:
    movzx eax, byte [ebp + rotor_pos + 1]
    movzx ecx, byte [ebp + rotor_ncnt + 1]
    lea edx, [ebp + rotor_wnotch + 2]
    call is_at_notch
    test al, al
    jz .step_done

    movzx eax, byte [ebp + rotor_pos + 1]
    inc eax
    call mod26
    mov [ebp + rotor_pos + 1], al

    movzx eax, byte [ebp + rotor_pos + 0]
    inc eax
    call mod26
    mov [ebp + rotor_pos + 0], al

.step_done:
    pop edx
    pop ecx
    pop ebx
    ret

; =============================================================================
; enigma_crypt — encrypt/decrypt a single character
;   in:  al = letter index (0–25)
;   out: al = encrypted letter index (0–25)
;
; Stack frame via ebp:
;   [ebp - 24] = loop counter (dword)
;   [ebp - 28] = current character (byte)
;   [ebp - 4]  = saved ebx
;   [ebp - 8]  = saved ecx
;   [ebp - 12] = saved edx
;   [ebp - 16] = saved esi
;   [ebp - 20] = saved edi
;   [ebp + 0]  = saved outer ebp  (= program base for data refs)
;   [ebp + 4]  = return address
; =============================================================================
; 7.6 enigma_crypt — encrypt/decrypt a single letter
; =============================================================================
; Input:  AL = letter index (0-25)
;         All Enigma state must be initialized
; Output: AL = encrypted/decrypted letter index (0-25)
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI, ESP
; =============================================================================
enigma_crypt:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi
    sub esp, 8                  ; [ebp-24] = counter, [ebp-28] = char

    mov edi, [ebp]              ; edi = outer ebp = program base
    mov [ebp - 28], al          ; save input character

    movzx eax, byte [edi + eax + plug_map]
    mov [ebp - 28], al          ; Update char after plugboard

    mov dword [ebp - 24], 2     ; loop counter = 2 (fast rotor index)
.fwd_main:
    cmp dword [ebp - 24], 0
    jl .fwd_thin
    mov ebx, [ebp - 24]         ; ebx = rotor index

    movzx eax, byte [ebp - 28]  ; eax = current char
    movzx ecx, byte [edi + ebx + rotor_pos]
    add eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    sub eax, ecx
    add eax, ALPHA2             ; 52
    xor edx, edx
    mov esi, 26
    div esi                     ; edx = offset into rotor wiring
    mov ecx, edx                ; save remainder in ecx
    imul ebx, 26
    add ebx, ecx
    movzx eax, byte [edi + ebx + rotor_fwd]

    mov ebx, [ebp - 24]
    movzx ecx, byte [edi + ebx + rotor_pos]
    sub eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    add eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov ecx, ALPHA
    div ecx
    mov [ebp - 28], dl

    dec dword [ebp - 24]
    jmp .fwd_main

.fwd_thin:
    mov ebx, 3
    movzx eax, byte [ebp - 28]
    movzx ecx, byte [edi + ebx + rotor_pos]
    add eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    sub eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov esi, 26
    div esi
    mov ecx, edx
    imul ebx, 26
    add ebx, ecx
    movzx eax, byte [edi + ebx + rotor_fwd]
    movzx ecx, byte [edi + rotor_pos + 3]
    sub eax, ecx
    movzx ecx, byte [edi + rotor_ring + 3]
    add eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov ecx, ALPHA
    div ecx
    mov [ebp - 28], dl

    movzx eax, byte [ebp - 28]
    movzx eax, byte [edi + eax + refl_wiring]
    mov [ebp - 28], al

    mov ebx, 3
    movzx eax, byte [ebp - 28]
    movzx ecx, byte [edi + ebx + rotor_pos]
    add eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    sub eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov esi, 26
    div esi
    mov ecx, edx
    imul ebx, 26
    add ebx, ecx
    movzx eax, byte [edi + ebx + rotor_rev]
    movzx ecx, byte [edi + rotor_pos + 3]
    sub eax, ecx
    movzx ecx, byte [edi + rotor_ring + 3]
    add eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov ecx, ALPHA
    div ecx
    mov [ebp - 28], dl

    mov dword [ebp - 24], 0
.bwd_main:
    cmp dword [ebp - 24], 2
    jg .bwd_done
    mov ebx, [ebp - 24]

    movzx eax, byte [ebp - 28]
    movzx ecx, byte [edi + ebx + rotor_pos]
    add eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    sub eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov esi, 26
    div esi
    mov ecx, edx
    imul ebx, 26
    add ebx, ecx
    movzx eax, byte [edi + ebx + rotor_rev]
    mov ebx, [ebp - 24]
    movzx ecx, byte [edi + ebx + rotor_pos]
    sub eax, ecx
    movzx ecx, byte [edi + ebx + rotor_ring]
    add eax, ecx
    add eax, ALPHA2
    xor edx, edx
    mov ecx, ALPHA
    div ecx
    mov [ebp - 28], dl

    inc dword [ebp - 24]
    jmp .bwd_main

.bwd_done:
    movzx eax, byte [ebp - 28]
    movzx eax, byte [edi + eax + plug_map]

    add esp, 8
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop ebp
    ret

; =============================================================================
; 8. KEY DERIVATION
; =============================================================================
; ---
; 8.1 hash_init — Initialize PRNG state from date value
; -----------------------------------------------------------------------------
; Input:  EAX = date value (YYYYMMDD as integer)
; Output: prng_state initialized
; Clobbers: EAX, EBX
; =============================================================================
hash_init:
    mov ebx, eax
    shl ebx, 7
    xor eax, ebx
    add eax, 0xDEADBEEF
    xor eax, ebx
    test eax, eax
    jnz .nonzero
    mov eax, 0x12345678
.nonzero:
    mov [ebp + prng_state], eax
    ret

; -----------------------------------------------------------------------------
; 8.2 xorshift32 — Generate next PRNG value
; -----------------------------------------------------------------------------
; Input:  prng_state must be set
; Output: EAX = next pseudo-random value
; Clobbers: EAX, EBX, EDX
; =============================================================================
xorshift32:
    mov eax, [ebp + prng_state]
    mov ebx, eax
    shl ebx, 13
    xor eax, ebx
    mov ebx, eax
    shr ebx, 17
    xor eax, ebx
    mov ebx, eax
    shl ebx, 5
    xor eax, ebx
    mov [ebp + prng_state], eax
    ret

; -----------------------------------------------------------------------------
; 8.3 hash_expand — Expand PRNG state to 32-byte buffer
; -----------------------------------------------------------------------------
; Input:  prng_state must be initialized
; Output: hash_buf filled with 8 × 32-bit values
; Clobbers: EAX, ECX, EDI
; =============================================================================
hash_expand:
    push ebx
    push ecx
    push edi
    lea edi, [ebp + hash_buf]
    mov ecx, 8
.expand_loop:
    call xorshift32
    mov [edi], eax
    add edi, 4
    dec ecx
    jnz .expand_loop
    pop edi
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; 8.4 derive_daily_key
; -----------------------------------------------------------------------------
; Input:  hash_buf must be filled (32 bytes)
; Output: daily_rotor_idx, daily_ring, daily_position, daily_reflector,
;         daily_plugs populated
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI
; =============================================================================
derive_daily_key:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    lea esi, [ebp + hash_buf]

    movzx eax, byte [esi]
    and eax, 1
    mov [ebp + daily_reflector], al

    movzx eax, byte [esi]
    shr eax, 1
    and eax, 1
    add eax, 8
    mov [ebp + daily_rotor_idx + 3], al

    mov dword [ebp + used_main_rotors], 0
    mov dword [ebp + used_main_rotors + 4], 0

    movzx eax, byte [esi + 1]
    xor edx, edx
    mov ecx, 8
    div ecx
    mov [ebp + daily_rotor_idx + 0], dl
    mov byte [ebp + used_main_rotors + edx], 1

    movzx eax, byte [esi + 2]
    xor edx, edx
    mov ecx, 7
    div ecx
    xor ecx, ecx
    xor edi, edi
.find_rotor2:
    cmp byte [ebp + used_main_rotors + edi], 0
    jne .fr2_next
    cmp ecx, edx
    je .fr2_found
    inc ecx
.fr2_next:
    inc edi
    cmp edi, 8
    jl .find_rotor2
.fr2_found:
    mov eax, edi
    mov [ebp + daily_rotor_idx + 1], al
    mov byte [ebp + used_main_rotors + edi], 1

    movzx eax, byte [esi + 3]
    xor edx, edx
    mov ecx, 6
    div ecx
    xor ecx, ecx
    xor edi, edi
.find_rotor3:
    cmp byte [ebp + used_main_rotors + edi], 0
    jne .fr3_next
    cmp ecx, edx
    je .fr3_found
    inc ecx
.fr3_next:
    inc edi
    cmp edi, 8
    jl .find_rotor3
.fr3_found:
    mov eax, edi
    mov [ebp + daily_rotor_idx + 2], al
    mov byte [ebp + used_main_rotors + edi], 1

    movzx eax, byte [esi + 4]
    xor edx, edx
    mov ecx, 26
    div ecx
    mov [ebp + daily_ring + 0], dl

    movzx eax, byte [esi + 5]
    xor edx, edx
    div ecx
    mov [ebp + daily_ring + 1], dl

    movzx eax, byte [esi + 6]
    xor edx, edx
    div ecx
    mov [ebp + daily_ring + 2], dl

    movzx eax, byte [esi + 7]
    xor edx, edx
    div ecx
    mov [ebp + daily_ring + 3], dl

    movzx eax, byte [esi + 8]
    xor edx, edx
    div ecx
    mov [ebp + daily_position + 0], dl

    movzx eax, byte [esi + 9]
    xor edx, edx
    div ecx
    mov [ebp + daily_position + 1], dl

    movzx eax, byte [esi + 10]
    xor edx, edx
    div ecx
    mov [ebp + daily_position + 2], dl

    movzx eax, byte [esi + 11]
    xor edx, edx
    div ecx
    mov [ebp + daily_position + 3], dl

    call derive_plugboard

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; =============================================================================
; -----------------------------------------------------------------------------
; 8.5 derive_plugboard
; -----------------------------------------------------------------------------
; Input:  hash_buf[12..31] contains random bytes
;         plug_alphabet initialized with 0-25
; Output: daily_plugs contains 10 stecker pairs
; Clobbers: EAX, EBX, ECX, EDX, ESI, EDI
; =============================================================================
derive_plugboard:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    lea edi, [ebp + plug_alphabet]
    xor ecx, ecx
.init_alpha:
    mov byte [edi + ecx], cl
    inc ecx
    cmp ecx, 26
    jl .init_alpha
    mov byte [ebp + plug_alphabet_cnt], 26

    xor ecx, ecx
.init_plugs:
    mov byte [ebp + daily_plugs + ecx], cl
    inc ecx
    cmp ecx, 26
    jl .init_plugs

    lea esi, [ebp + hash_buf]
    add esi, 12
    xor edi, edi

.plug_loop:
    cmp edi, 10
    jge .plug_done

    movzx eax, byte [esi]
    movzx ecx, byte [ebp + plug_alphabet_cnt]
    xor edx, edx
    div ecx
    movzx ebx, byte [ebp + plug_alphabet + edx]
    mov byte [ebp + daily_plugs + ebx], bl

    movzx ecx, byte [ebp + plug_alphabet_cnt]
    dec ecx
    mov al, [ebp + plug_alphabet + ecx]
    mov [ebp + plug_alphabet + edx], al
    mov byte [ebp + plug_alphabet_cnt], cl

    movzx eax, byte [esi + 1]
    movzx ecx, byte [ebp + plug_alphabet_cnt]
    xor edx, edx
    div ecx
    movzx eax, byte [ebp + plug_alphabet + edx]

    mov [ebp + daily_plugs + ebx], al
    mov [ebp + daily_plugs + eax], bl

    movzx ecx, byte [ebp + plug_alphabet_cnt]
    dec ecx
    mov al, [ebp + plug_alphabet + ecx]
    mov [ebp + plug_alphabet + edx], al
    mov byte [ebp + plug_alphabet_cnt], cl

    add esi, 2
    inc edi
    jmp .plug_loop

.plug_done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; 8.6 derive_message_key
; -----------------------------------------------------------------------------
; Input:  hash_buf[22..25] contains random bytes
; Output: message_key populated with 4 letters (0-25)
; Clobbers: EAX, EDX, ECX, ESI
; =============================================================================
derive_message_key:
    push esi
    lea esi, [ebp + hash_buf]
    add esi, 22

    movzx eax, byte [esi]
    xor edx, edx
    mov ecx, 26
    div ecx
    mov [ebp + message_key + 0], dl

    movzx eax, byte [esi + 1]
    xor edx, edx
    div ecx
    mov [ebp + message_key + 1], dl

    movzx eax, byte [esi + 2]
    xor edx, edx
    div ecx
    mov [ebp + message_key + 2], dl

    movzx eax, byte [esi + 3]
    xor edx, edx
    div ecx
    mov [ebp + message_key + 3], dl

    pop esi
    ret

; -----------------------------------------------------------------------------
; 8.7 apply_message_key
; -----------------------------------------------------------------------------
; Input:  message_key must be set
; Output: daily_position updated
; Clobbers: EAX, ECX
; =============================================================================
apply_message_key:
    movzx eax, byte [ebp + message_key + 0]
    mov [ebp + daily_position + 0], al
    movzx eax, byte [ebp + message_key + 1]
    mov [ebp + daily_position + 1], al
    movzx eax, byte [ebp + message_key + 2]
    mov [ebp + daily_position + 2], al
    movzx eax, byte [ebp + message_key + 3]
    mov [ebp + daily_position + 3], al
    ret

; =============================================================================
; 9. RODATA SECTION
; =============================================================================
; ---
; 9.1 Strings and messages
; -----------------------------------------------------------------------------
banner:
    db "ENIGMA M4", 0

prompt_date: db 13, "Enter date (YYYYMMDD): ", 0
prompt_msg:  db "> ", 0
space_str:   db " ", 0
separator:   db " | ", 0

msg_daily_key:
    db "Daily key (derived from date):", 0
msg_rotors:     db "  Rotors (slow-mid-fast-thin): ", 0
msg_reflector:  db "  Reflector:                    ", 0
msg_rings:      db "  Ring settings:                ", 0
msg_positions:  db "  Grundstellung:                ", 0
msg_plugs:      db "  Plugboard pairs:              ", 0
msg_message_key: db "Message key:    ", 0
msg_indicator:   db "Indicator:      ", 0
msg_interactive: db 13, "Type plaintext to encrypt. Empty line to quit.", 13, 0
msg_help:       db 13, "=== ENIGMA M4 HELP ===", 13, 0
msg_help1:      db "OVERVIEW:", 13, 0
msg_help2:      db "  Kriegsmarine M4 (4-rotor) Enigma simulator", 13, 0
msg_help3:      db "  Used by German Navy U-boats (1942-1945)", 13, 0
msg_help4:      db 13, "USAGE MODES:", 13, 0
msg_help5:      db "  Interactive: enigma", 13, 0
msg_help6:      db "  CLI encrypt: enigma e YYYYMMDD MESSAGE", 13, 0
msg_help7:      db "  CLI decrypt: enigma d YYYYMMDD MESSAGE", 13, 0
msg_help8:      db 13, "INTERACTIVE COMMANDS:", 13, 0
msg_help9:      db "  A-Z, a-z  Encrypt typed letter (Space=encode 'X')", 13, 0
msg_help10:     db "  Enter      Newline (submit message)", 13, 0
msg_help11:     db "  ? or h     Show this help", 13, 0
msg_help12:     db "  Empty line Quit", 13, 0
msg_help13:     db 13, "KEY DERIVATION:", 13, 0
msg_help14:     db "  Date (YYYYMMDD) → xorshift PRNG → daily key", 13, 0
msg_help15:     db "  Same date = same settings (reproducible)", 13, 0
msg_help16:     db 13, "ENIGMA COMPONENTS:", 13, 0
msg_help17:     db "  4 rotors (3 main + 1 thin)", 13, 0
msg_help18:     db "  Reflector (B or C)", 13, 0
msg_help19:     db "  Plugboard (max 10 pairs)", 13, 0
msg_help20:     db 13, "NOTE: Encryption = Decryption (symmetric)", 0

; Main rotor wiring tables (10 rotors × 26 bytes = 260 bytes)
rotor_wiring:
.L0:    db 'EKMFLGDQVZNTOWYHXUSPAIBRCJ'       ; Rotor I
.L1:    db 'AJDKSIRUXBLHWTMCQGZNPYFVOE'       ; Rotor II
.L2:    db 'BDFHJLCPRTXVZNYEIWGAKMUSQO'       ; Rotor III
.L3:    db 'ESOVPZJAYQUIRHXLNFTGKDCMWB'       ; Rotor IV
.L4:    db 'VZBRGITYUPSDNHLXAWMJQOFECK'       ; Rotor V
.L5:    db 'JPGVOUMFYQBENHZRDKASXLICTW'       ; Rotor VI
.L6:    db 'NZJHGRCXMYSWBOUFAIVLPEKQDT'       ; Rotor VII
.L7:    db 'FKQHTLXOCBJSPDZRAMEWNIUYGV'       ; Rotor VIII
.L8:    db 'LEYJVCNIXWPBQMDRTAKZGFUHOS'       ; Rotor Beta (thin)
.L9:    db 'FSOKANUERHMBTIYCWLQPZXVGJD'       ; Rotor Gamma (thin)

; Rotor notch positions
rotor_notch:
        db 16                    ; I
        db 4                     ; II
        db 21                    ; III
        db 9                     ; IV
        db 25                    ; V
        db 25, 12                ; VI
        db 25, 12                ; VII
        db 25, 12                ; VIII
        db 0                     ; Beta
        db 0                     ; Gamma

rotor_notch_count:
        db 1, 1, 1, 1, 1, 2, 2, 2, 0, 0

; Reflector wiring (thin B and C)
reflector_wiring:
.LB:    db 'ENKQAUYWJICOPBLMDXZVFTHRGS'       ; Reflector B
.LC:    db 'RDOBJNTKVEHMLFCWZAXGYIPSUQ'       ; Reflector C

; Rotor names
rotor_names:
        db 'I', 0
        db 'I', 'I', 0
        db 'I', 'I', 'I', 0
        db 'I', 'V', 0
        db 'V', 0
        db 'V', 'I', 0
        db 'V', 'I', 'I', 0
        db 'V', 'I', 'I', 'I', 0
        db 'B', 'e', 't', 'a', 0
        db 'G', 'a', 'm', 'm', 'a', 0

; Word offsets into rotor_names
rotor_name_offsets:
        dw 0, 2, 5, 9, 11, 13, 16, 20, 24, 30

; Letters
letters: db 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'

; =============================================================================
; Mutable state (in-file zero buffer per OS ABI rule 3)
; =============================================================================
align 4

input_buf:    times 32 db 0
letter_buf:   db 0
char_in:      db 0
char_out:     db 0
date_val:     dd 0
indicator:    times 8 db 0

; Argument buffers
arg_buf_1:    times 32 db 0
arg_buf_2:    times 32 db 0
arg_buf_3:    times 256 db 0

prng_state:   dd 0
hash_buf:     times 32 db 0

daily_rotor_idx:    times 4 db 0
daily_ring:         times 4 db 0
daily_position:     times 4 db 0
daily_reflector:    db 0
daily_plugs:        times 26 db 0
message_key:        times 4 db 0

rotor_fwd:      times 104 db 0
rotor_rev:      times 104 db 0
rotor_ring:     times 4 db 0
rotor_pos:      times 4 db 0
rotor_wnotch:   times 8 db 0
rotor_ncnt:     times 4 db 0
refl_wiring:    times 26 db 0
plug_map:       times 26 db 0

plug_alphabet:     times 26 db 0
plug_alphabet_cnt: db 0
used_main_rotors:  times 8 db 0