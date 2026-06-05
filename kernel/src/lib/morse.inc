; =============================================================================
; morse.inc — Morse code playback for x86 OS userland programs
; =============================================================================
; Depends on sound.inc (provides sound_beep / sys_sound 48)
; Plays Morse code via the PC speaker using standard timing:
;   Dot  = 100ms (10 ticks), Dash = 300ms (30 ticks)
;   Gap between elements = 100ms, Gap between characters = 300ms
; =============================================================================

%ifndef MORSE_INC
%define MORSE_INC

%include "sound.inc"

; Timing constants (in 10ms ticks, matching sys_sound's ecx unit)
%define DOT_DURATION     10      ; 100ms
%define DASH_DURATION    30      ; 300ms (3 × dot)
%define ELEMENT_GAP      10      ; 100ms between dots/dashes
%define CHAR_GAP         30      ; 300ms between characters (3 × dot)

; =============================================================================
; Morse code lookup table (ROData)
; Each entry: [length (1 byte), pattern (1 byte)]
; Pattern: MSB-first bitmask, 0 = dot, 1 = dash
; A-Z at indices 0–25, 0–9 at indices 26–35
; =============================================================================
section .rodata
morse_table:
    ; A-Z (0-25)
    db 2, 0x01          ; A: .-     (0b01)
    db 4, 0x08          ; B: -...   (0b1000)
    db 4, 0x0A          ; C: -.-.   (0b1010)
    db 3, 0x04          ; D: -..    (0b100)
    db 1, 0x00          ; E: .      (0b0)
    db 4, 0x02          ; F: ..-.   (0b0010)
    db 3, 0x06          ; G: --.    (0b110)
    db 4, 0x00          ; H: ....   (0b0000)
    db 2, 0x00          ; I: ..     (0b00)
    db 4, 0x07          ; J: .---   (0b0111)
    db 3, 0x05          ; K: -.-    (0b101)
    db 4, 0x04          ; L: .-..   (0b0100)
    db 2, 0x03          ; M: --     (0b11)
    db 2, 0x02          ; N: -.     (0b10)
    db 3, 0x07          ; O: ---    (0b111)
    db 4, 0x06          ; P: .--.   (0b0110)
    db 4, 0x0D          ; Q: --.-   (0b1101)
    db 3, 0x02          ; R: .-.    (0b010)
    db 3, 0x00          ; S: ...    (0b000)
    db 1, 0x01          ; T: -      (0b1)
    db 3, 0x01          ; U: ..-    (0b001)
    db 4, 0x01          ; V: ...-   (0b0001)
    db 3, 0x03          ; W: .--    (0b011)
    db 4, 0x09          ; X: -..-   (0b1001)
    db 4, 0x0B          ; Y: -.--   (0b1011)
    db 4, 0x0C          ; Z: --..   (0b1100)

    ; 0-9 (26-35)
    db 5, 0x1F          ; 0: ----- (0b11111)
    db 5, 0x0F          ; 1: .---- (0b01111)
    db 5, 0x07          ; 2: ..--- (0b00111)
    db 5, 0x03          ; 3: ...-- (0b00011)
    db 5, 0x01          ; 4: ....- (0b00001)
    db 5, 0x00          ; 5: ..... (0b00000)
    db 5, 0x10          ; 6: -.... (0b10000)
    db 5, 0x18          ; 7: --... (0b11000)
    db 5, 0x1C          ; 8: ---.. (0b11100)
    db 5, 0x1E          ; 9: ----. (0b11110)

; =============================================================================
; morse_lookup — get Morse pattern for a character
; Input:  al = ASCII character (A-Z, a-z, 0-9)
; Output: al = pattern byte, ah = length (0 if invalid char)
; Clobbers: ebx, ecx
; =============================================================================
morse_lookup:
    ; Convert to uppercase if lowercase
    cmp al, 'a'
    jl .check_digit
    cmp al, 'z'
    jg .invalid
    sub al, 32              ; lowercase → uppercase

.check_digit:
    ; Check if A-Z
    cmp al, 'A'
    jl .check_num
    cmp al, 'Z'
    jg .invalid
    sub al, 'A'             ; al = 0-25
    jmp .get_entry

.check_num:
    cmp al, '0'
    jl .invalid
    cmp al, '9'
    jg .invalid
    sub al, '0'
    add al, 26              ; al = 26-35 (digits offset)
    jmp .get_entry

.invalid:
    xor ax, ax              ; ah=0, al=0 → invalid
    ret

.get_entry:
    ; al = index into morse_table (each entry is 2 bytes: length, pattern)
    mov ebx, morse_table
    shl eax, 1              ; eax = index * 2
    add ebx, eax
    mov ah, [ebx]           ; ah = length
    mov al, [ebx + 1]       ; al = pattern
    ret

; =============================================================================
; morse_play_element — play one dot or dash
; Input:  dl = 0 (dot) or 1 (dash)
; Clobbers: ebx, ecx, edx
; =============================================================================
morse_play_element:
    test dl, dl
    jz .play_dot

.play_dash:
    mov ebx, 600            ; Dash frequency (adjust as needed)
    mov ecx, DASH_DURATION
    call sound_beep
    jmp .element_gap

.play_dot:
    mov ebx, 800            ; Dot frequency (adjust as needed)
    mov ecx, DOT_DURATION
    call sound_beep

.element_gap:
    ; Wait for element gap (silence)
    mov ecx, ELEMENT_GAP
    call sound_silence
    ret

; =============================================================================
; morse_play_char — play one character as Morse code
; Input:  al = ASCII character (A-Z, a-z, 0-9)
; Clobbers: eax, ebx, ecx, edx, esi, edi
; =============================================================================
morse_play_char:
    call morse_lookup
    test ah, ah
    jz .done               ; Invalid character, skip

    ; Save pattern and length
    ; al = pattern, ah = length
    movzx esi, al          ; esi = pattern (8-bit zero-extended to 32-bit)
    movzx edi, ah          ; edi = length (8-bit zero-extended to 32-bit)
    xor ecx, ecx           ; ecx = bit index (0 = first element, MSB first)

.loop:
    cmp ecx, edi
    jge .char_gap

    ; Get current element (MSB first)
    ; For element i (0-based, first = MSB):
    ;   shift pattern right by (length-1 - i) to get the bit in LSB
    mov eax, edi           ; eax = length
    sub eax, 1             ; eax = length - 1
    sub eax, ecx           ; eax = (length-1 - i)
    mov ebx, esi           ; ebx = pattern
    mov cl, al             ; cl = shift count (using cl for variable shift)
    shr ebx, cl            ; shift right by (length-1 - i)
    and ebx, 1             ; ebx = current bit (0=dot, 1=dash)
    mov edx, ebx           ; edx = 0 (dot) or 1 (dash)
    call morse_play_element

    inc ecx
    jmp .loop

.char_gap:
    mov ecx, CHAR_GAP
    call sound_silence

.done:
    ret

; =============================================================================
; morse_play_string — play a null-terminated string as Morse code
; Input:  esi = pointer to null-terminated string
; Clobbers: eax, ecx, edx, esi
; =============================================================================
morse_play_string:
    push esi

.loop:
    lodsb                   ; al = next character
    test al, al
    jz .done
    call morse_play_char
    jmp .loop

.done:
    pop esi
    ret

%endif
