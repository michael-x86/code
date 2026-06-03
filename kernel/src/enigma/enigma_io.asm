; ============================================================================
; enigma_io.asm — Display helpers, formatting, syscalls
; ============================================================================
; Pure I/O helpers. No Enigma state involved.
;
; Exported symbols:
;   io_print(s)               — print a null-terminated string to stdout
;   io_print_n(s, n)          — print n bytes
;   io_readline(buf, n)       — read a line from stdin, null-terminate
;   io_print_int(val)         — print unsigned 32-bit integer in decimal
;   io_print_letter(idx)      — print single letter A–Z from index 0–25
;   io_newline()              — print "\n"
;   io_prompt_date()          — print "Enter date (YYYYMMDD): "
;   io_prompt_msg()           — print "> "
; ============================================================================

section .data

prompt_date: db "Enter date (YYYYMMDD): ", 0
prompt_date_len equ $ - prompt_date - 1

prompt_msg:  db "> ", 0
prompt_msg_len equ $ - prompt_msg - 1

section .bss

digit_buf: resb 12           ; enough for 32-bit unsigned + null
letter_buf: resb 1

section .text

global io_print
global io_print_n
global io_readline
global io_print_int
global io_print_letter
global io_newline
global io_prompt_date
global io_prompt_msg

; Linux syscalls (i386)
%define SYS_WRITE 4
%define SYS_READ  3
%define SYS_EXIT  1
%define STDOUT    1
%define STDIN     0

; ============================================================================
; io_print — print null-terminated string
; Input: ESI = pointer to string
; ============================================================================
io_print:
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi

    ; Compute length
    mov     ecx, esi
.len_loop:
    cmp     byte [ecx], 0
    je      .len_done
    inc     ecx
    jmp     .len_loop
.len_done:
    sub     ecx, esi            ; ECX = length
    mov     edx, ecx            ; EDX = count
    mov     ecx, esi            ; ECX = buffer pointer

    mov     eax, SYS_WRITE
    mov     ebx, STDOUT
    int     0x80

    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    ret

; ============================================================================
; io_print_n — print N bytes
; Input: ESI = pointer, ECX = count
; ============================================================================
io_print_n:
    push    eax
    push    ebx
    push    edx
    mov     eax, SYS_WRITE
    mov     ebx, STDOUT
    mov     edx, ecx            ; EDX = count
    mov     ecx, esi            ; ECX = buffer pointer
    int     0x80
    pop     edx
    pop     ebx
    pop     eax
    ret

; ============================================================================
; io_readline — read a line from stdin
; Input: ESI = buffer, ECX = buffer size
; Returns: EAX = bytes read (including newline)
; Replaces newline with null terminator
; ============================================================================
io_readline:
    push    ebx
    push    edx
    push    esi

    mov     edx, ecx            ; EDX = count
    mov     ecx, esi            ; ECX = buffer
    mov     eax, SYS_READ
    mov     ebx, STDIN
    int     0x80
    mov     edx, eax            ; EDX = bytes read

    ; Strip newline
    test    eax, eax
    jle     .read_done
    mov     ecx, eax
    dec     ecx
    mov     byte [esi + ecx], 0

.read_done:
    mov     eax, edx
    pop     esi
    pop     edx
    pop     ebx
    ret

; ============================================================================
; io_print_int — print unsigned 32-bit integer in decimal
; Input: EAX = value
; ============================================================================
io_print_int:
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi

    lea     esi, [digit_buf + 11]   ; write backwards
    mov     byte [esi], 0           ; null terminator
    mov     ebx, 10
    test    eax, eax
    jnz     .int_loop
    ; Special case: 0
    dec     esi
    mov     byte [esi], '0'
    jmp     .int_print

.int_loop:
    test    eax, eax
    jz      .int_print
    xor     edx, edx
    div     ebx
    add     edx, '0'
    dec     esi
    mov     byte [esi], dl
    jmp     .int_loop

.int_print:
    call    io_print

    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    ret

; ============================================================================
; io_print_letter — print single letter A–Z from index 0–25
; Input: AL = index
; ============================================================================
io_print_letter:
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi

    add     al, 'A'
    mov     [letter_buf], al
    mov     eax, SYS_WRITE
    mov     ebx, STDOUT
    mov     ecx, letter_buf
    mov     edx, 1
    int     0x80

    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    ret

; ============================================================================
; io_newline — print newline
; ============================================================================
io_newline:
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi

    mov     eax, SYS_WRITE
    mov     ebx, STDOUT
    lea     ecx, [digit_buf]       ; reuse buffer for a single "\n"
    mov     byte [digit_buf], 0x0A
    mov     edx, 1
    int     0x80

    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    ret

; ============================================================================
; io_prompt_date — print "Enter date (YYYYMMDD): "
; ============================================================================
io_prompt_date:
    push    esi
    push    ecx
    lea     esi, [prompt_date]
    mov     ecx, prompt_date_len
    call    io_print_n
    pop     ecx
    pop     esi
    ret

; ============================================================================
; io_prompt_msg — print "> "
; ============================================================================
io_prompt_msg:
    push    esi
    push    ecx
    lea     esi, [prompt_msg]
    mov     ecx, prompt_msg_len
    call    io_print_n
    pop     ecx
    pop     esi
    ret
