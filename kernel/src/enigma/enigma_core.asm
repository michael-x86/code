; ============================================================================
; enigma_core.asm — Rotor stepping, single-char encrypt, plugboard
; ============================================================================
; The heart of the Enigma machine simulation.
;
; Call enigma_init() once at startup to load daily key into working state.
; Call enigma_step() before each character to advance rotors.
; Call enigma_crypt() to encrypt/decrypt a single letter.
;
; Rotor indices:
;   [0] = slow   (leftmost main)
;   [1] = middle
;   [2] = fast   (rightmost main, steps every keypress)
;   [3] = thin   (Beta or Gamma, never steps)
; ============================================================================

section .bss

rotor_fwd:      resb 104        ; 26 bytes * 4 rotors
rotor_rev:      resb 104        ; 26 bytes * 4 rotors
rotor_ring:     resb 4
rotor_pos:      resb 4
rotor_wnotch:   resb 8          ; 2 bytes * 4 rotors (working copy)
rotor_ncnt:     resb 4
refl_wiring:    resb 26
plug_map:       resb 26

section .data

extern daily_rotor_idx
extern daily_ring
extern daily_position
extern daily_reflector
extern daily_plugs
extern message_key

extern rotor_wiring
extern rotor_notch
extern rotor_notch_count
extern reflector_wiring

%define ALPHA   26
%define ALPHA2  52

section .text

global enigma_init
global enigma_reset
global enigma_step
global enigma_crypt

; ============================================================================
; mod26 — EAX = EAX mod 26, result in [0,25]
; Clobbers EDX
; ============================================================================
mod26:
    push    ebx
    xor     edx, edx
    mov     ebx, ALPHA
    div     ebx
    mov     eax, edx
    pop     ebx
    ret

; ============================================================================
; enigma_init — copy daily key into working state, precompute reverse wiring
; ============================================================================
enigma_init:
    push    ebp
    mov     ebp, esp
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    xor     ebx, ebx            ; working rotor index 0-3

.init_rotor:
    cmp     ebx, 4
    jge     .init_refl

    ; Get rotor type (0-9)
    movzx   eax, byte [daily_rotor_idx + ebx]

    ; Copy forward wiring: rotor_wiring[type*26] → rotor_fwd[rbx*26]
    imul    eax, 26
    lea     esi, [rotor_wiring + eax]
    mov     edi, ebx
    imul    edi, 26
    lea     edi, [rotor_fwd + edi]
    push    ecx
    mov     ecx, 26
    rep movsb
    pop     ecx

    ; Precompute reverse wiring
    ; Reload source pointer (esi was advanced by rep movsb)
    movzx   eax, byte [daily_rotor_idx + ebx]
    imul    eax, 26
    lea     esi, [rotor_wiring + eax]
    mov     edi, ebx
    imul    edi, 26
    lea     edi, [rotor_rev + edi]
    xor     ecx, ecx
.rev_loop:
    cmp     ecx, ALPHA
    jge     .rev_done
    movzx   eax, byte [esi + ecx]   ; fwd[i] = letter ('A'-'Z')
    sub     eax, 'A'                 ; normalize to 0-25
    mov     byte [edi + eax], cl     ; rev[letter] = i
    inc     ecx
    jmp     .rev_loop
.rev_done:

    ; Copy ring setting and position
    movzx   eax, byte [daily_ring + ebx]
    mov     [rotor_ring + ebx], al
    movzx   eax, byte [daily_position + ebx]
    mov     [rotor_pos + ebx], al

    ; Copy notch data
    movzx   eax, byte [daily_rotor_idx + ebx]
    cmp     eax, 8
    jge     .no_notch           ; Beta/Gamma have no notches

    ; Compute offset into packed notch table.
    ; Packed layout: I(1B), II(1B), III(1B), IV(1B), V(1B), VI(2B), VII(2B), VIII(2B)
    ; Offsets by type: 0, 1, 2, 3, 4, 5, 7, 9
    ; Formula: offset = type + max(0, type - 4)
    push    ebx
    mov     edi, eax            ; edi = type (will become table offset)
    cmp     eax, 4
    jle     .offset_ok
    add     edi, eax
    sub     edi, 4              ; edi = type + (type - 4) for types 5-7
.offset_ok:
    movzx   ecx, byte [rotor_notch_count + eax]  ; eax still = type
    mov     [rotor_ncnt + ebx], cl               ; write to working index
    movzx   eax, byte [rotor_notch + edi]         ; read from packed source table
    mov     [rotor_wnotch + ebx * 2], al           ; write to local working copy
    pop     ebx
    jmp     .next_rotor

.no_notch:
    mov     [rotor_ncnt + ebx], byte 0

.next_rotor:
    inc     ebx
    jmp     .init_rotor

.init_refl:
    ; Copy reflector wiring
    movzx   eax, byte [daily_reflector]
    imul    eax, 26
    lea     esi, [reflector_wiring + eax]
    lea     edi, [refl_wiring]
    push    ecx
    mov     ecx, 26
    rep movsb
    pop     ecx

    ; Copy plugboard
    lea     esi, [daily_plugs]
    lea     edi, [plug_map]
    mov     ecx, 26
    rep movsb

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     ebp
    ret

; ============================================================================
; enigma_reset — reset rotor positions to daily_position values
; ============================================================================
enigma_reset:
    push    esi
    push    edi
    push    ecx
    lea     esi, [daily_position]
    lea     edi, [rotor_pos]
    mov     ecx, 4
    rep movsb
    pop     ecx
    pop     edi
    pop     esi
    ret

; ============================================================================
; enigma_step — advance rotors before encoding a character
;
; Rules:
;   1. Fast rotor (2) always steps.
;   2. If fast rotor at notch → middle (1) steps.
;   3. If middle rotor at notch → slow (0) + middle (1) step (double-step).
;   4. Thin rotor (3) never steps.
; ============================================================================
enigma_step:
    push    ebx
    push    ecx
    push    edx

    ; Step fast rotor (2)
    movzx   eax, byte [rotor_pos + 2]
    inc     eax
    call    mod26
    mov     [rotor_pos + 2], al

    ; Check fast rotor notch
    movzx   eax, byte [rotor_pos + 2]
    movzx   ecx, byte [rotor_ncnt + 2]
    lea     edx, [rotor_wnotch + 4]   ; rotor 2 notch at offset 4
    call    is_at_notch
    test    al, al
    jz      .check_mid

    ; Step middle rotor (1)
    movzx   eax, byte [rotor_pos + 1]
    inc     eax
    call    mod26
    mov     [rotor_pos + 1], al

.check_mid:
    ; Check middle rotor notch
    movzx   eax, byte [rotor_pos + 1]
    movzx   ecx, byte [rotor_ncnt + 1]
    lea     edx, [rotor_wnotch + 2]   ; rotor 1 notch at offset 2
    call    is_at_notch
    test    al, al
    jz      .step_done

    ; Double-step: middle + slow
    movzx   eax, byte [rotor_pos + 1]
    inc     eax
    call    mod26
    mov     [rotor_pos + 1], al

    movzx   eax, byte [rotor_pos + 0]
    inc     eax
    call    mod26
    mov     [rotor_pos + 0], al

.step_done:
    pop     edx
    pop     ecx
    pop     ebx
    ret

; ----------------------------------------------------------------------------
; is_at_notch — check if position AL matches any notch
; Input:  AL = position, ECX = notch count, EDX = pointer to notch bytes
; Output: AL = 1 if at notch, 0 if not
; ----------------------------------------------------------------------------
is_at_notch:
    push    ebx
    test    ecx, ecx
    jz      .notch_no
.notch_loop:
    cmp     al, [edx]
    je      .notch_yes
    inc     edx
    dec     ecx
    jnz     .notch_loop
.notch_no:
    xor     eax, eax
    pop     ebx
    ret
.notch_yes:
    mov     al, 1
    pop     ebx
    ret

; ============================================================================
; enigma_crypt — encrypt or decrypt a single character
;
; Input:  AL = letter index 0–25
; Output: AL = encrypted letter 0–25
;
; Signal path:
;   plug → fast(2) → mid(1) → slow(0) → thin(3) → reflector
;   → thin(3) → slow(0) → mid(1) → fast(2) → plug
;
; Rotor transform (forward):
;   effective = (signal + pos - ring) mod 26
;   mapped = wiring[effective]
;   signal = (mapped - pos + ring) mod 26
;
; Rotor transform (backward): same but use rev_wiring
; ============================================================================
enigma_crypt:
    push    ebp
    mov     ebp, esp
    sub     esp, 8              ; [ebp-4]=signal, [ebp-8]=rotor index
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    mov     [ebp - 4], al      ; save input signal

    ; --- Plugboard forward ---
    movzx   eax, byte [plug_map + eax]
    mov     [ebp - 4], al

    ; --- Forward through main rotors: 2, 1, 0 ---
    mov     dword [ebp - 8], 2
.fwd_main:
    cmp     dword [ebp - 8], 0
    jl      .fwd_thin
    mov     ebx, [ebp - 8]

    ; effective = (signal + pos - ring) mod 26
    movzx   eax, byte [ebp - 4]
    movzx   ecx, byte [rotor_pos + ebx]
    add     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    sub     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    push    ebx
    mov     ebx, ALPHA
    div     ebx
    pop     ebx
    ; EDX = effective
    push    edx

    ; mapped = rotor_fwd[rotor * 26 + effective]
    imul    ebx, 26
    pop     edx
    movzx   eax, byte [rotor_fwd + ebx + edx]

    ; signal = (mapped - pos + ring) mod 26
    mov     ebx, [ebp - 8]
    movzx   ecx, byte [rotor_pos + ebx]
    sub     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    add     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    mov     ecx, ALPHA
    div     ecx
    mov     [ebp - 4], dl

    dec     dword [ebp - 8]
    jmp     .fwd_main

.fwd_thin:
    ; --- Forward through thin rotor (3) ---
    mov     ebx, 3
    movzx   eax, byte [ebp - 4]
    movzx   ecx, byte [rotor_pos + ebx]
    add     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    sub     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    push    ebx
    mov     ebx, ALPHA
    div     ebx
    pop     ebx
    push    edx                 ; save effective
    imul    ebx, 26
    pop     edx
    movzx   eax, byte [rotor_fwd + ebx + edx]
    ; Undo offset
    movzx   ecx, byte [rotor_pos + 3]
    sub     eax, ecx
    movzx   ecx, byte [rotor_ring + 3]
    add     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    mov     ecx, ALPHA
    div     ecx
    mov     [ebp - 4], dl

    ; --- Reflector ---
    movzx   eax, byte [ebp - 4]
    movzx   eax, byte [refl_wiring + eax]
    mov     [ebp - 4], al

    ; --- Backward through thin rotor (3) ---
    mov     ebx, 3
    movzx   eax, byte [ebp - 4]
    movzx   ecx, byte [rotor_pos + ebx]
    add     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    sub     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    push    ebx
    mov     ebx, ALPHA
    div     ebx
    pop     ebx
    push    edx
    imul    ebx, 26
    pop     edx
    movzx   eax, byte [rotor_rev + ebx + edx]
    movzx   ecx, byte [rotor_pos + 3]
    sub     eax, ecx
    movzx   ecx, byte [rotor_ring + 3]
    add     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    mov     ecx, ALPHA
    div     ecx
    mov     [ebp - 4], dl

    ; --- Backward through main rotors: 0, 1, 2 ---
    mov     dword [ebp - 8], 0
.bwd_main:
    cmp     dword [ebp - 8], 2
    jg      .bwd_done
    mov     ebx, [ebp - 8]

    movzx   eax, byte [ebp - 4]
    movzx   ecx, byte [rotor_pos + ebx]
    add     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    sub     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    push    ebx
    mov     ebx, ALPHA
    div     ebx
    pop     ebx
    push    edx
    imul    ebx, 26
    pop     edx
    movzx   eax, byte [rotor_rev + ebx + edx]
    mov     ebx, [ebp - 8]
    movzx   ecx, byte [rotor_pos + ebx]
    sub     eax, ecx
    movzx   ecx, byte [rotor_ring + ebx]
    add     eax, ecx
    add     eax, ALPHA2
    xor     edx, edx
    mov     ecx, ALPHA
    div     ecx
    mov     [ebp - 4], dl

    inc     dword [ebp - 8]
    jmp     .bwd_main

.bwd_done:
    ; --- Plugboard (symmetric) ---
    movzx   eax, byte [ebp - 4]
    movzx   eax, byte [plug_map + eax]

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    mov     esp, ebp
    pop     ebp
    ret
