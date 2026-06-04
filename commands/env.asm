; env — print the values currently applied from /etc/config
;
; Reads each config slot via sys_get_config (ebx = key 0..3, edi = dst)
; and prints a human-readable form so the user can verify the kernel
; actually picked up /etc/config. Keys:
;   0 = timezone (raw int from /etc/config, in hours)
;   1 = datefmt  (0=YYYY-MM-DD, 1=DD/MM/YYYY, 2=MM/DD/YYYY)
;   2 = timefmt  (0=24h, 1=12h)
;   3 = layout   (0=us, 1=sv)
;
; The header and label lines are printed with sys_print_cr (CR in the
; string is converted to a real newline). The numeric value returned
; by sys_get_config is NUL-terminated with no embedded CR, so we use
; sys_print to emit it. We then emit a manual newline (sys_newline)
; so the next label starts on a fresh line.
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

    ; --- Header (title + first label "  layout   = ") ---
    lea esi, [ebp + header]
    mov eax, 2              ; sys_print_cr
    int 0x80

    ; --- layout (key 3) — label already in header ---
    mov ebx, 3
    call .fetch_and_print
    call .newline

    ; --- timezone (key 0) ---
    lea esi, [ebp + lbl_tz]
    mov eax, 2
    int 0x80
    mov ebx, 0
    call .fetch_and_print
    call .newline

    ; --- datefmt (key 1) ---
    lea esi, [ebp + lbl_df]
    mov eax, 2
    int 0x80
    mov ebx, 1
    call .fetch_and_print
    call .newline

    ; --- timefmt (key 2) ---
    lea esi, [ebp + lbl_tf]
    mov eax, 2
    int 0x80
    mov ebx, 2
    call .fetch_and_print
    call .newline
    ret

; --- helpers --------------------------------------------------------------

; .fetch_and_print — call sys_get_config into buf, then sys_print the digits
;   in:  ebx = key (0..3)
;   out: writes NUL-terminated decimal into buf, prints it
.fetch_and_print:
    lea edi, [ebp + buf]
    mov eax, 30             ; sys_get_config
    int 0x80
    cmp eax, -1
    je .fetch_err
    lea esi, [ebp + buf]
    mov eax, 1              ; sys_print
    int 0x80
    ret
.fetch_err:
    lea esi, [ebp + err_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

.newline:
    mov eax, 3              ; sys_newline
    int 0x80
    ret

; --- data -----------------------------------------------------------------
; header packs the title and the first label on the same string so we
; can emit both with a single sys_print_cr (the 0x0D becomes a newline).
header:     db "applied /etc/config values:", 13
            db "  layout   = ", 0
lbl_tz:     db "  timezone = ", 0
lbl_df:     db "  datefmt  = ", 0
lbl_tf:     db "  timefmt  = ", 0
err_msg:    db "<unavailable>", 13, 0

section .bss
alignb 4
buf: resb 16
