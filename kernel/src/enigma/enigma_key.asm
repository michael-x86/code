; ============================================================================
; enigma_key.asm — Date parsing, daily key derivation, message key derivation
; ============================================================================
; Exported symbols:
;   parse_date(buf, len)       — parse "YYYYMMDD" string → EAX = integer
;   derive_daily_key()         — use hash_buf to fill all rotor/plug globals
;   derive_message_key()       — extract 4-letter message key from hash_buf
;   apply_message_key()        — set rotor positions to the derived message key
;
; Exported data:
;   daily_rotor_idx[4]         — 4 rotor indices (slow, mid, fast, thin)
;   daily_ring[4]              — 4 ring settings (0–25)
;   daily_position[4]          — 4 initial positions (0–25, Grundstellung)
;   daily_reflector            — 0=B, 1=C
;   daily_plugs[26]            — forward plugboard mapping (letter→letter)
;   message_key[4]             — 4-letter message key (0–25 each)
; ============================================================================

section .data

global daily_rotor_idx
global daily_ring
global daily_position
global daily_reflector
global daily_plugs
global message_key

; Daily key configuration (filled by derive_daily_key)
daily_rotor_idx:    times 4 db 0     ; [0]=slow, [1]=mid, [2]=fast, [3]=thin
daily_ring:         times 4 db 0     ; ring settings
daily_position:     times 4 db 0     ; Grundstellung (initial positions)
daily_reflector:    db 0             ; 0=B, 1=C

; Plugboard: symmetric 26-byte mapping. plugs[i] = what letter i swaps with.
; If plugs[i] == i, the letter is unswapped.
daily_plugs:        times 26 db 0

; Message key
message_key:        times 4 db 0

; Used-rotor tracking during derivation
used_main_rotors:   times 8 db 0     ; 0=available, 1=used

; Temporary alphabet for plugboard derivation
plug_alphabet:      times 26 db 0
plug_alphabet_cnt:  db 26

; External references
extern hash_buf
extern letters
extern rotor_name_offsets
extern NUM_MAIN_ROTORS
extern NUM_PLUG_PAIRS

section .text

global parse_date
global derive_daily_key
global derive_message_key
global apply_message_key

; Linux syscalls
%define SYS_WRITE 4
%define SYS_READ  3
%define A_OFFSET 0

; ----------------------------------------------------------------------------
; parse_date — convert "YYYYMMDD" string to 32-bit integer
; Input:  ESI = pointer to string (at least 8 chars), ECX = length
; Output: EAX = parsed integer
; Clobbers: EBX, ECX, EDX, ESI
; ----------------------------------------------------------------------------
parse_date:
    xor     eax, eax            ; result = 0
    mov     ecx, 8              ; always consume exactly 8 digits
.parse_loop:
    movzx   ebx, byte [esi]
    sub     ebx, '0'            ; convert ASCII digit to number
    imul    eax, eax, 10        ; result *= 10
    add     eax, ebx            ; result += digit
    inc     esi
    dec     ecx
    jnz     .parse_loop
    ret

; ----------------------------------------------------------------------------
; derive_daily_key — extract daily Enigma settings from hash_buf
;
; hash_buf layout:
;   byte  0:   reflector (bit 0) + thin rotor (bit 1)
;   byte  1:   main rotor 1 (slow) selection
;   byte  2:   main rotor 2 (middle) selection
;   byte  3:   main rotor 3 (fast) selection
;   bytes 4-7: ring settings (4 bytes)
;   bytes 8-11: Grundstellung (4 bytes)
;   bytes 12-21: plugboard (10 pairs from alphabet)
;   bytes 22-25: message key
; ----------------------------------------------------------------------------
derive_daily_key:
    push    ebp
    mov     ebp, esp
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    mov     esi, hash_buf

    ; --- Reflector (byte 0, bit 0) ---
    movzx   eax, byte [esi]
    and     eax, 1              ; 0=B, 1=C
    mov     [daily_reflector], al

    ; --- Thin rotor (byte 0, bit 1) ---
    movzx   eax, byte [esi]
    shr     eax, 1
    and     eax, 1              ; 0 or 1
    add     eax, 8              ; 8=Beta, 9=Gamma
    mov     [daily_rotor_idx + 3], al

    ; --- Main rotors (bytes 1-3) ---
    ; Clear used-rotor tracking
    mov     dword [used_main_rotors], 0
    mov     dword [used_main_rotors + 4], 0

    ; Rotor 1 (slow, leftmost) — pick from 8
    movzx   eax, byte [esi + 1]
    xor     edx, edx
    mov     ecx, NUM_MAIN_ROTORS    ; 8
    div     ecx                     ; edx = index 0-7
    mov     [daily_rotor_idx + 0], dl
    mov     byte [used_main_rotors + edx], 1

    ; Rotor 2 (middle) — pick from remaining 7
    movzx   eax, byte [esi + 2]
    xor     edx, edx
    mov     ecx, 7
    div     ecx                     ; edx = 0-6 → map to actual unused rotor
    ; Find the (edx)th unused rotor
    xor     ecx, ecx                ; count of unused seen
    xor     edi, edi                ; rotor index
.find_rotor2:
    cmp     byte [used_main_rotors + edi], 0
    jne     .fr2_next
    cmp     ecx, edx
    je      .fr2_found
    inc     ecx
.fr2_next:
    inc     edi
    cmp     edi, NUM_MAIN_ROTORS
    jl      .find_rotor2
.fr2_found:
    mov     [daily_rotor_idx + 1], edi
    mov     byte [used_main_rotors + edi], 1

    ; Rotor 3 (fast) — pick from remaining 6
    movzx   eax, byte [esi + 3]
    xor     edx, edx
    mov     ecx, 6
    div     ecx
    xor     ecx, ecx
    xor     edi, edi
.find_rotor3:
    cmp     byte [used_main_rotors + edi], 0
    jne     .fr3_next
    cmp     ecx, edx
    je      .fr3_found
    inc     ecx
.fr3_next:
    inc     edi
    cmp     edi, NUM_MAIN_ROTORS
    jl      .find_rotor3
.fr3_found:
    mov     [daily_rotor_idx + 2], edi
    mov     byte [used_main_rotors + edi], 1

    ; --- Ring settings (bytes 4-7) ---
    movzx   eax, byte [esi + 4]
    xor     edx, edx
    mov     ecx, 26
    div     ecx
    mov     [daily_ring + 0], dl

    movzx   eax, byte [esi + 5]
    xor     edx, edx
    div     ecx
    mov     [daily_ring + 1], dl

    movzx   eax, byte [esi + 6]
    xor     edx, edx
    div     ecx
    mov     [daily_ring + 2], dl

    movzx   eax, byte [esi + 7]
    xor     edx, edx
    div     ecx
    mov     [daily_ring + 3], dl

    ; --- Grundstellung / initial positions (bytes 8-11) ---
    movzx   eax, byte [esi + 8]
    xor     edx, edx
    div     ecx
    mov     [daily_position + 0], dl

    movzx   eax, byte [esi + 9]
    xor     edx, edx
    div     ecx
    mov     [daily_position + 1], dl

    movzx   eax, byte [esi + 10]
    xor     edx, edx
    div     ecx
    mov     [daily_position + 2], dl

    movzx   eax, byte [esi + 11]
    xor     edx, edx
    div     ecx
    mov     [daily_position + 3], dl

    ; --- Plugboard (bytes 12-21) ---
    call    derive_plugboard

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; derive_plugboard — select 10 plugboard pairs from hash_buf bytes 12-21
;
; Algorithm: maintain a list of available letters. For each of 10 pairs,
; pick two letters from the available list using hash bytes as indices.
; ----------------------------------------------------------------------------
derive_plugboard:
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    ; Initialize plug_alphabet = [A, B, C, ..., Z]
    mov     edi, plug_alphabet
    xor     ecx, ecx
.init_alpha:
    mov     byte [edi + ecx], cl
    inc     ecx
    cmp     ecx, 26
    jl      .init_alpha
    mov     byte [plug_alphabet_cnt], 26

    ; Initialize all plugs to identity (self-mapping)
    xor     ecx, ecx
.init_plugs:
    mov     byte [daily_plugs + ecx], cl
    inc     ecx
    cmp     ecx, 26
    jl      .init_plugs

    ; Derive 10 pairs
    mov     esi, hash_buf
    add     esi, 12             ; start at byte 12
    xor     edi, edi            ; pair counter

.plug_loop:
    cmp     edi, NUM_PLUG_PAIRS
    jge     .plug_done

    ; Pick first letter
    movzx   eax, byte [esi]     ; hash byte for first letter
    movzx   ecx, byte [plug_alphabet_cnt]
    xor     edx, edx
    div     ecx                 ; edx = index into available alphabet
    movzx   ebx, byte [plug_alphabet + edx]  ; EBX = first letter (0-25)
    mov     byte [daily_plugs + ebx], bl      ; temporarily store

    ; Remove first letter from available list (swap with last, shrink)
    movzx   ecx, byte [plug_alphabet_cnt]
    dec     ecx                 ; last index
    mov     al, [plug_alphabet + ecx]
    mov     [plug_alphabet + edx], al
    mov     byte [plug_alphabet_cnt], cl

    ; Pick second letter
    movzx   eax, byte [esi + 1] ; hash byte for second letter
    movzx   ecx, byte [plug_alphabet_cnt]
    xor     edx, edx
    div     ecx
    movzx   eax, byte [plug_alphabet + edx]  ; EAX = second letter (0-25)

    ; Set plugboard pair: first↔second
    mov     [daily_plugs + ebx], al           ; first maps to second
    mov     [daily_plugs + eax], bl           ; second maps to first

    ; Remove second letter from available list
    movzx   ecx, byte [plug_alphabet_cnt]
    dec     ecx
    mov     dl, [plug_alphabet + ecx]
    mov     [plug_alphabet + edx], dl
    mov     byte [plug_alphabet_cnt], cl

    add     esi, 2              ; next pair of hash bytes
    inc     edi                 ; next pair
    jmp     .plug_loop

.plug_done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret

; ----------------------------------------------------------------------------
; derive_message_key — extract 4-letter message key from hash_buf bytes 22-25
; ----------------------------------------------------------------------------
derive_message_key:
    push    esi
    mov     esi, hash_buf
    add     esi, 22

    movzx   eax, byte [esi]
    xor     edx, edx
    mov     ecx, 26
    div     ecx
    mov     [message_key + 0], dl

    movzx   eax, byte [esi + 1]
    xor     edx, edx
    div     ecx
    mov     [message_key + 1], dl

    movzx   eax, byte [esi + 2]
    xor     edx, edx
    div     ecx
    mov     [message_key + 2], dl

    movzx   eax, byte [esi + 3]
    xor     edx, edx
    div     ecx
    mov     [message_key + 3], dl

    pop     esi
    ret

; ----------------------------------------------------------------------------
; apply_message_key — set rotor positions to the derived message key
; This is called after the indicator is encrypted under the daily Grundstellung.
; ----------------------------------------------------------------------------
apply_message_key:
    movzx   eax, byte [message_key + 0]
    mov     [daily_position + 0], al
    movzx   eax, byte [message_key + 1]
    mov     [daily_position + 1], al
    movzx   eax, byte [message_key + 2]
    mov     [daily_position + 2], al
    movzx   eax, byte [message_key + 3]
    mov     [daily_position + 3], al
    ret
