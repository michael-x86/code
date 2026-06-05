; =============================================================================
; morse.asm — Interactive Morse Code Testing Program
; =============================================================================
; Features:
;   - Interactive text input field
;   - Real-time Morse code display
;   - Play Morse via PC speaker (using morse.inc + sound.inc)
;   - Button navigation (Left/Right/Tab) at row 22
;   - Buttons: [PLAY] [CLEAR] [EXIT]
; =============================================================================

[bits 32]
[org 0x00000000]

%include "userland.inc"
%include "morse.inc"

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
    call main_loop

    ret

; =============================================================================
; Draw the initial UI
; =============================================================================
draw_ui:
    ; Print title and instructions
    lea esi, [ebp + title_msg]
    SYS_PRINT_CR

    lea esi, [ebp + instr_msg]
    SYS_PRINT_CR

    lea esi, [ebp + button_msg]
    SYS_PRINT_CR

    lea esi, [ebp + nav_msg]
    SYS_PRINT_CR

    lea esi, [ebp + separator]
    SYS_PRINT_CR

    ; Input label
    lea esi, [ebp + input_label]
    SYS_PRINT

    call update_input_display

    ; Morse output label
    lea esi, [ebp + morse_label]
    SYS_PRINT

    mov dword [ebp + morse_len], 0
    call update_morse_display

    ; Draw buttons
    call draw_buttons

    ret

; =============================================================================
; Update the input display (show current input buffer)
; =============================================================================
update_input_display:
    lea edi, [ebp + input_buf]
    call print_string

    ; Clear remaining input area (40 chars max)
    mov ecx, 40
    sub ecx, [ebp + input_len]
    test ecx, ecx
    jle .done
    mov ebx, ' '
.clear_loop:
    push ecx
    mov eax, 0              ; SYS_PUTCHAR
    int 0x80
    pop ecx
    loop .clear_loop

.done:
    ret

; =============================================================================
; Update the Morse code display
; =============================================================================
update_morse_display:
    lea esi, [ebp + input_buf]
    call text_to_morse_string

    lea esi, [ebp + morse_buf]
    call print_string

    ret

; =============================================================================
; Convert text in input_buf to Morse code string in morse_buf
; =============================================================================
text_to_morse_string:
    push esi
    push edi
    push ecx
    push eax

    lea edi, [ebp + morse_buf]
    xor ecx, ecx                ; input index

.loop:
    mov al, [esi + ecx]
    test al, al
    jz .done

    ; Convert character to Morse
    call morse_lookup
    test ah, ah
    jz .skip_char

    ; We have pattern in al, length in ah
    mov dl, ah                 ; dl = length
    mov dh, al                 ; dh = pattern
    xor ebx, ebx               ; ebx = bit index

.element_loop:
    cmp bl, dl
    jge .element_done

    ; Extract current element (MSB first)
    mov al, dh                 ; al = pattern
    mov ah, 0
    mov cl, dl                 ; cl = length
    dec cl                     ; cl = length - 1
    sub cl, bl                 ; cl = (length-1) - bit_index
    shr al, cl                 ; shift so target bit is LSB
    and al, 1                  ; extract bit

    test al, al
    jz .is_dot

.is_dash:
    mov byte [edi], '-'
    jmp .next_element

.is_dot:
    mov byte [edi], '.'

.next_element:
    inc edi
    inc ebx
    jmp .element_loop

.element_done:
    ; Add space between characters
    mov byte [edi], ' '
    inc edi

.skip_char:
    inc ecx
    jmp .loop

.done:
    mov byte [edi], 0          ; null terminate
    pop eax
    pop ecx
    pop edi
    pop esi
    ret

; =============================================================================
; Draw buttons
; =============================================================================
draw_buttons:
    lea esi, [ebp + separator]
    SYS_PRINT_CR

    mov eax, [ebp + selected_button]
    test eax, eax
    jz .play_selected

    dec eax
    jz .clear_selected

    ; Exit selected
    lea esi, [ebp + btn_play]
    SYS_PRINT
    lea esi, [ebp + btn_clear]
    SYS_PRINT
    lea esi, [ebp + btn_exit_sel]
    SYS_PRINT_CR
    ret

.clear_selected:
    lea esi, [ebp + btn_play]
    SYS_PRINT
    lea esi, [ebp + btn_clear_sel]
    SYS_PRINT
    lea esi, [ebp + btn_exit]
    SYS_PRINT_CR
    ret

.play_selected:
    lea esi, [ebp + btn_play_sel]
    SYS_PRINT
    lea esi, [ebp + btn_clear]
    SYS_PRINT
    lea esi, [ebp + btn_exit]
    SYS_PRINT_CR
    ret

; =============================================================================
; Main interaction loop
; =============================================================================
main_loop:
    mov eax, 7                  ; SYS_GETKEY
    int 0x80

    cmp al, 27                 ; ESC
    je .exit

    cmp al, 13                 ; Enter
    je .select_button

    cmp al, 9                  ; Tab
    je .next_button

    cmp al, 75                 ; Left arrow (scancode)
    je .prev_button

    cmp al, 77                 ; Right arrow
    je .next_button

    ; Regular character input (printable)
    cmp al, ' '
    jl main_loop

    cmp al, 'z'
    jg main_loop

    call is_printable
    test eax, eax
    jz main_loop

    ; Add to input buffer
    call add_char

    jmp main_loop

.select_button:
    mov eax, [ebp + selected_button]
    test eax, eax
    jz .do_play

    dec eax
    jz .do_clear

    ; Exit
    ret

.do_play:
    call play_morse
    jmp main_loop

.do_clear:
    call clear_input
    jmp main_loop

.next_button:
    inc dword [ebp + selected_button]
    cmp dword [ebp + selected_button], 3
    jl .redraw
    mov dword [ebp + selected_button], 0

.redraw:
    call draw_buttons
    jmp main_loop

.prev_button:
    dec dword [ebp + selected_button]
    cmp dword [ebp + selected_button], 0
    jge .redraw
    mov dword [ebp + selected_button], 2
    jmp main_loop

.exit:
    ret

; =============================================================================
; Check if character is printable (A-Z, a-z, 0-9, space)
; =============================================================================
is_printable:
    cmp al, ' '
    je .yes

    cmp al, '0'
    jl .no

    cmp al, '9'
    jle .yes

    cmp al, 'A'
    jl .no

    cmp al, 'Z'
    jle .yes

    cmp al, 'a'
    jl .no

    cmp al, 'z'
    jle .yes

.no:
    xor eax, eax
    ret

.yes:
    mov eax, 1
    ret

; =============================================================================
; Add character to input buffer
; =============================================================================
add_char:
    mov ecx, [ebp + input_len]
    cmp ecx, 39                ; max 39 chars (leave room for null)
    jge .full

    lea edi, [ebp + input_buf]
    mov [edi + ecx], al
    inc dword [ebp + input_len]
    mov byte [edi + ecx + 1], 0

    call update_input_display
    call update_morse_display

.full:
    ret

; =============================================================================
; Clear input buffer
; =============================================================================
clear_input:
    mov dword [ebp + input_len], 0
    mov byte [ebp + input_buf], 0
    call update_input_display
    call update_morse_display
    ret

; =============================================================================
; Play Morse code for current input
; =============================================================================
play_morse:
    lea esi, [ebp + input_buf]
    call morse_play_string
    ret

; =============================================================================
; Print null-terminated string (esi = pointer)
; =============================================================================
print_string:
    push eax
    push ebx

.loop:
    lodsb
    test al, al
    jz .done
    mov ebx, eax
    mov eax, 0                ; SYS_PUTCHAR
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
instr_msg:      db "Type text to encode (A-Z, 0-9)", 13, 0
button_msg:     db "Buttons: [PLAY] [CLEAR] [EXIT]", 13, 0
nav_msg:        db "Navigate: Left/Right/Tab, Enter/Space to select", 13, 0
separator:      db "---", 13, 0
input_label:    db "Input: ", 0
morse_label:    db "Morse: ", 0

btn_play:       db "[PLAY] ", 0
btn_clear:      db "[CLEAR] ", 0
btn_exit:       db "[EXIT]", 13, 0
btn_play_sel:   db "[>PLAY<] ", 0
btn_clear_sel:  db "[>CLEAR<] ", 0
btn_exit_sel:   db "[>EXIT<]", 13, 0

selected_button:  dd 0        ; 0=PLAY, 1=CLEAR, 2=EXIT
input_len:        dd 0
morse_len:        dd 0

input_buf:        times 41 db 0
morse_buf:        times 200 db 0
