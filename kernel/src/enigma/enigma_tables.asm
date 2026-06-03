; ============================================================================
; enigma_tables.asm — Rotor wiring, notch positions, and reflector tables
; ============================================================================
; M4 Enigma rotor data. All wiring strings are 26 bytes representing the
; forward permutation: wiring[i] = letter that position i (0='A') maps to.
;
; In the M4:
;   - 3 main rotors are chosen from {I, II, III, IV, V, VI, VII, VIII}
;   - 1 thin rotor is chosen from {Beta, Gamma}
;   - 1 thin reflector is chosen from {B, C}
;
; Rotor indices for runtime array [0..7]:
;   0=I, 1=II, 2=III, 3=IV, 4=V, 5=VI, 6=VII, 7=VIII
;   8=Beta, 9=Gamma
; ============================================================================

section .data

global rotor_wiring
global rotor_notch
global rotor_notch_count
global reflector_wiring
global reflector_name

; ----------------------------------------------------------------------------
; Main rotor wiring tables (forward direction; 'A'=0 .. 'Z'=25 notation)
; Each entry: 26 ASCII letters showing the substitution.
;
; Example: Rotor I, input A (position 0) → output E,
;          input B (position 1) → output K, etc.
; ----------------------------------------------------------------------------
rotor_wiring:
.L0:    db 'EKMFLGDQVZNTOWYHXUSPAIBRCJ'       ; Rotor I
.L1:    db 'AJDKSIRUXBLHWTMCQGZNPYFVOE'       ; Rotor II
.L2:    db 'BDFHJLCPRTXVZNYEIWGAKMUSQO'       ; Rotor III
.L3:    db 'ESOVPZJAYQUIRHXLNFTGKDCMWB'       ; Rotor IV
.L4:    db 'VZBRGITYUPSDNHLXAWMJQOFECK'       ; Rotor V
.L5:    db 'JPGVOUMFYQBENHZRDKASXLICTW'       ; Rotor VI
.L6:    db 'NZJHGRCXMYSWBOUFAIVLPEKQDT'       ; Rotor VII
.L7:    db 'FKQHTLXOCBJSPDZRAMEWNIUYGV'       ; Rotor VIII

; ----------------------------------------------------------------------------
; Thin rotor wiring (Beta and Gamma) — used in the 4th (leftmost) position
; ----------------------------------------------------------------------------
.L8:    db 'LEYJVCNIXWPBQMDRTAKZGFUHOS'       ; Rotor Beta
.L9:    db 'FSOKANUERHMBTIYCWLQPZXVGJD'       ; Rotor Gamma

; ----------------------------------------------------------------------------
; Rotor notch positions (0-indexed: A=0, B=1, ..., Z=25)
; When the rotor steps INTO this position, it triggers the next rotor.
; Rotors VI, VII, VIII have TWO notches (Z and M).
; ----------------------------------------------------------------------------
rotor_notch:
        db 16                    ; I:   Q
        db 4                     ; II:  E
        db 21                    ; III: V
        db 9                     ; IV:  J
        db 25                    ; V:   Z
        db 25, 12                ; VI:  Z, M
        db 25, 12                ; VII: Z, M
        db 25, 12                ; VIII:Z, M
        db 0                     ; Beta:  (no notch — never steps)
        db 0                     ; Gamma: (no notch — never steps)

; How many notches each rotor has
rotor_notch_count:
        db 1, 1, 1, 1, 1, 2, 2, 2, 0, 0

; ----------------------------------------------------------------------------
; Reflector wiring (thin reflectors B and C)
; Reflectors are fixed symmetric substitutions (involutions).
; ----------------------------------------------------------------------------
reflector_wiring:
.LB:    db 'ENKQAUYWJICOPBLMDXZVFTHRGS'       ; Reflector B (thin)
.LC:    db 'RDOBJNTKVEHMLFCWZAXGYIPSUQ'       ; Reflector C (thin)

; Reflector names for display
reflector_name:
.LB_n:  db 'B', 0
.LC_n:  db 'C', 0

; ----------------------------------------------------------------------------
; Rotor names for display
; ----------------------------------------------------------------------------
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

; Offsets into rotor_names for each rotor index
rotor_name_offsets:
        dw 0, 2, 5, 9, 11, 13, 16, 20, 24, 30

; ----------------------------------------------------------------------------
; Letters table (for converting 0–25 → 'A'–'Z')
; ----------------------------------------------------------------------------
letters: db 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'

; ----------------------------------------------------------------------------
; Number of main rotors in the pool (for key derivation selection)
; ----------------------------------------------------------------------------
global NUM_MAIN_ROTORS
global NUM_THIN_ROTORS
global NUM_PLUG_PAIRS
global ALPHA_SIZE
NUM_MAIN_ROTORS equ 8
NUM_THIN_ROTORS equ 2
NUM_PLUG_PAIRS equ 10
ALPHA_SIZE     equ 26
