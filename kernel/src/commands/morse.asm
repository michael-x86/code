; =============================================================================
; morse.asm — Display Morse code as text
; =============================================================================

[bits 32]
[org 0x00000000]

%include "userland.inc"

%define SYS_PUTCHAR   0
%define SYS_GETKEY    7
%define BUFFER_SIZE   64

; =============================================================================
; Entry point
; =============================================================================
global _start
_start:
    USERLAND_START

    ; Clear screen
    mov eax, 4              ; SYS_CLS
    int 0x80

    call draw_ui

    ; Print initial prompt
    lea esi, [ebp + input_label]
    SYS_PRINT_CR

    ; Initialize buffer index
    xor ecx, ecx            ; ECX = buffer index

.main_loop:
    mov eax, SYS_GETKEY
    int 0x80

    test al, al              ; Check if key = 0 (no key)
    jz .main_loop

    cmp al, 'q'             ; quit on 'q'
    je .quit

    cmp al, 'Q'
    je .quit

    cmp al, 13              ; Enter key
    je .process_input

    cmp al, 10              ; LF
    je .process_input

    cmp al, 8               ; Backspace
    je .handle_backspace

    cmp al, 32              ; Space (printable)
    jl .skip_unprintable

    ; Printable character - echo it
    movzx ebx, al
    mov eax, SYS_PUTCHAR
    int 0x80

    ; Store in buffer
    lea edi, [ebp + input_buffer]
    mov [edi + ecx], al
    inc ecx

    jmp .main_loop

.handle_backspace:
    test ecx, ecx
    jz .main_loop

    dec ecx

    mov ebx, 8
    mov eax, SYS_PUTCHAR
    int 0x80

    jmp .main_loop

.skip_unprintable:
    jmp .main_loop

.process_input:
    ; Newline
    mov eax, 3
    int 0x80

    ; Convert buffer to Morse code and display
    call display_morse

    ; Reset buffer
    xor ecx, ecx

    ; Reprint prompt
    lea esi, [ebp + input_label]
    SYS_PRINT_CR

    jmp .main_loop

.quit:
    mov eax, 3              ; newline
    int 0x80

    ret

; =============================================================================
; Draw the UI
; =============================================================================
draw_ui:
    lea esi, [ebp + title_msg]
    SYS_PRINT_CR

    lea esi, [ebp + instr_msg]
    SYS_PRINT_CR

    lea esi, [ebp + separator]
    SYS_PRINT_CR

    ret

; =============================================================================
; Display Morse code for text in input_buffer
; =============================================================================
display_morse:
    push eax
    push ebx
    push ecx
    push esi
    push edi

    lea esi, [ebp + input_buffer]

.next_char:
    lodsb
    test al, al
    jz .done

    ; Convert to uppercase
    cmp al, 'a'
    jb .check_char
    cmp al, 'z'
    ja .check_char
    sub al, 32

.check_char:
    ; Check if it's A-Z or 0-9
    cmp al, 'A'
    jb .skip_char
    cmp al, 'Z'
    jbe .print_morse

    cmp al, '0'
    jb .skip_char
    cmp al, '9'
    jbe .print_morse

.skip_char:
    jmp .next_char

.print_morse:
    ; Print Morse code for character in AL
    call print_char_morse

    ; Space between letters
    mov ebx, ' '
    mov eax, SYS_PUTCHAR
    int 0x80

    jmp .next_char

.done:
    pop edi
    pop esi
    pop ecx
    pop ebx
    pop eax
    ret

; =============================================================================
; Print Morse code for a single character
; Input: AL = character (A-Z or 0-9)
; =============================================================================
print_char_morse:
    push eax
    push ebx
    push esi

    ; Check which character and print its Morse code
    cmp al, 'A'
    je .morse_A
    cmp al, 'B'
    je .morse_B
    cmp al, 'C'
    je .morse_C
    cmp al, 'D'
    je .morse_D
    cmp al, 'E'
    je .morse_E
    cmp al, 'F'
    je .morse_F
    cmp al, 'G'
    je .morse_G
    cmp al, 'H'
    je .morse_H
    cmp al, 'I'
    je .morse_I
    cmp al, 'J'
    je .morse_J
    cmp al, 'K'
    je .morse_K
    cmp al, 'L'
    je .morse_L
    cmp al, 'M'
    je .morse_M
    cmp al, 'N'
    je .morse_N
    cmp al, 'O'
    je .morse_O
    cmp al, 'P'
    je .morse_P
    cmp al, 'Q'
    je .morse_Q
    cmp al, 'R'
    je .morse_R
    cmp al, 'S'
    je .morse_S
    cmp al, 'T'
    je .morse_T
    cmp al, 'U'
    je .morse_U
    cmp al, 'V'
    je .morse_V
    cmp al, 'W'
    je .morse_W
    cmp al, 'X'
    je .morse_X
    cmp al, 'Y'
    je .morse_Y
    cmp al, 'Z'
    je .morse_Z
    cmp al, '0'
    je .morse_0
    cmp al, '1'
    je .morse_1
    cmp al, '2'
    je .morse_2
    cmp al, '3'
    je .morse_3
    cmp al, '4'
    je .morse_4
    cmp al, '5'
    je .morse_5
    cmp al, '6'
    je .morse_6
    cmp al, '7'
    je .morse_7
    cmp al, '8'
    je .morse_8
    cmp al, '9'
    je .morse_9

    jmp .done

.morse_A:
    lea esi, [ebp + str_A]
    jmp .print
.morse_B:
    lea esi, [ebp + str_B]
    jmp .print
.morse_C:
    lea esi, [ebp + str_C]
    jmp .print
.morse_D:
    lea esi, [ebp + str_D]
    jmp .print
.morse_E:
    lea esi, [ebp + str_E]
    jmp .print
.morse_F:
    lea esi, [ebp + str_F]
    jmp .print
.morse_G:
    lea esi, [ebp + str_G]
    jmp .print
.morse_H:
    lea esi, [ebp + str_H]
    jmp .print
.morse_I:
    lea esi, [ebp + str_I]
    jmp .print
.morse_J:
    lea esi, [ebp + str_J]
    jmp .print
.morse_K:
    lea esi, [ebp + str_K]
    jmp .print
.morse_L:
    lea esi, [ebp + str_L]
    jmp .print
.morse_M:
    lea esi, [ebp + str_M]
    jmp .print
.morse_N:
    lea esi, [ebp + str_N]
    jmp .print
.morse_O:
    lea esi, [ebp + str_O]
    jmp .print
.morse_P:
    lea esi, [ebp + str_P]
    jmp .print
.morse_Q:
    lea esi, [ebp + str_Q]
    jmp .print
.morse_R:
    lea esi, [ebp + str_R]
    jmp .print
.morse_S:
    lea esi, [ebp + str_S]
    jmp .print
.morse_T:
    lea esi, [ebp + str_T]
    jmp .print
.morse_U:
    lea esi, [ebp + str_U]
    jmp .print
.morse_V:
    lea esi, [ebp + str_V]
    jmp .print
.morse_W:
    lea esi, [ebp + str_W]
    jmp .print
.morse_X:
    lea esi, [ebp + str_X]
    jmp .print
.morse_Y:
    lea esi, [ebp + str_Y]
    jmp .print
.morse_Z:
    lea esi, [ebp + str_Z]
    jmp .print
.morse_0:
    lea esi, [ebp + str_0]
    jmp .print
.morse_1:
    lea esi, [ebp + str_1]
    jmp .print
.morse_2:
    lea esi, [ebp + str_2]
    jmp .print
.morse_3:
    lea esi, [ebp + str_3]
    jmp .print
.morse_4:
    lea esi, [ebp + str_4]
    jmp .print
.morse_5:
    lea esi, [ebp + str_5]
    jmp .print
.morse_6:
    lea esi, [ebp + str_6]
    jmp .print
.morse_7:
    lea esi, [ebp + str_7]
    jmp .print
.morse_8:
    lea esi, [ebp + str_8]
    jmp .print
.morse_9:
    lea esi, [ebp + str_9]
    jmp .print

.print:
    call print_string

.done:
    pop esi
    pop ebx
    pop eax
    ret

; =============================================================================
; Print a null-terminated string
; Input: ESI = string pointer
; =============================================================================
print_string:
    push eax
    push ebx

.loop:
    lodsb
    test al, al
    jz .done
    movzx ebx, al
    mov eax, SYS_PUTCHAR
    int 0x80
    jmp .loop

.done:
    pop ebx
    pop eax
    ret

; =============================================================================
; Data section
; =============================================================================
section .data

title_msg:      db "=== Morse Code Tester ===", 13, 0
instr_msg:      db "Type text to see Morse code (Enter=convert, q=quit)", 13, 0
separator:      db "---", 13, 0
input_label:    db "Input: ", 0

; Morse code strings
str_A:       db ".-", 0
str_B:       db "-...", 0
str_C:       db "-.-.", 0
str_D:       db "-..", 0
str_E:       db ".", 0
str_F:       db "..-.", 0
str_G:       db "--.", 0
str_H:       db "....", 0
str_I:       db "..", 0
str_J:       db ".---", 0
str_K:       db "-.-", 0
str_L:       db ".-..", 0
str_M:       db "--", 0
str_N:       db "-.", 0
str_O:       db "---", 0
str_P:       db ".--.", 0
str_Q:       db "--.-", 0
str_R:       db ".-.", 0
str_S:       db "...", 0
str_T:       db "-", 0
str_U:       db "..-", 0
str_V:       db "...-", 0
str_W:       db ".--", 0
str_X:       db "-..-", 0
str_Y:       db "-.--", 0
str_Z:       db "--..", 0
str_0:       db "-----", 0
str_1:       db ".----", 0
str_2:       db "..---", 0
str_3:       db "...--", 0
str_4:       db "....-", 0
str_5:       db ".....", 0
str_6:       db "-....", 0
str_7:       db "--...", 0
str_8:       db "---..", 0
str_9:       db "----.", 0

; =============================================================================
; BSS section
; =============================================================================
section .bss

input_buffer:   resb BUFFER_SIZE
