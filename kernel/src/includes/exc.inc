; =============================================================================
; exc.inc — Exception Stub Macros and Tables
; =============================================================================
;
; x86 CPU exceptions 0-31. Some push an error code, some don't.
; We define two macros:
;   ISR_NOERR n  — exception n does NOT push an error code
;   ISR_ERR n    — exception n DOES push an error code
;
; Both push a consistent frame (dummy 0 or real error code, then vector#),
; then jump to common_exception_handler.
;
; Exception vector numbers that push an error code (Intel SDM Vol 3, Table 6-1):
;   #DF (8), #TS (10), #NP (11), #SS (12), #GP (13), #PF (14), #AC (17)
;
; All others (0-7, 9, 15, 16, 18-31) do NOT push an error code.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; ISR_NOERR macro — exception that does NOT push an error code
; Usage: %define ISR_NOERR(n) ... then call it for each vector
; -----------------------------------------------------------------------------
%macro ISR_NOERR 1
isr%1:
    push dword 0            ; dummy error code
    push dword %1           ; exception vector number
    jmp common_exception_handler
%endmacro

; -----------------------------------------------------------------------------
; ISR_ERR macro — exception that DOES push an error code
; The CPU already pushed the error code, so we just push the vector#
; -----------------------------------------------------------------------------
%macro ISR_ERR 1
isr%1:
    push dword %1           ; exception vector number
    jmp common_exception_handler
%endmacro

; -----------------------------------------------------------------------------
; Generate all 32 exception stubs
; -----------------------------------------------------------------------------

; Exceptions without error code (0-7)
ISR_NOERR 0   ; #DE Divide Error
ISR_NOERR 1   ; #DB Debug
ISR_NOERR 2   ; NMI
ISR_NOERR 3   ; #BP Breakpoint
ISR_NOERR 4   ; #OF Overflow
ISR_NOERR 5   ; #BR Bound Range
ISR_NOERR 6   ; #UD Invalid Opcode
ISR_NOERR 7   ; #NM Device Not Available

; Exception 8: #DF Double Fault (has error code)
ISR_ERR 8

; Exception 9: #CO Coprocessor Segment Overrun (no error code, legacy)
ISR_NOERR 9

; Exceptions with error code (10-14)
ISR_ERR 10    ; #TS Invalid TSS
ISR_ERR 11    ; #NP Segment Not Present
ISR_ERR 12    ; #SS Stack Segment Fault
ISR_ERR 13    ; #GP General Protection Fault
ISR_ERR 14    ; #PF Page Fault

; Exception 15: #RES Reserved (no error code)
ISR_NOERR 15

; Exception 16: #MF x87 FPU Error (no error code)
ISR_NOERR 16

; Exception 17: #AC Alignment Check (has error code)
ISR_ERR 17

; Exceptions 18-31 (no error codes, most are reserved)
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XM SIMD Floating Point
ISR_NOERR 20   ; #VE Virtualization
ISR_NOERR 21   ; #CP Control Protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; -----------------------------------------------------------------------------
; Exception name table — 32 null-terminated 2-char strings indexed by vector
; Used by panic handler to print exception names
; -----------------------------------------------------------------------------
section .data

exc_names:
    db "DE", 0, 0    ; 0  #DE
    db "DB", 0, 0    ; 1  #DB
    db "NM", 0, 0    ; 2  NMI
    db "BP", 0, 0    ; 3  #BP
    db "OF", 0, 0    ; 4  #OF
    db "BR", 0, 0    ; 5  #BR
    db "UD", 0, 0    ; 6  #UD
    db "NM", 0, 0    ; 7  #NM
    db "DF", 0, 0    ; 8  #DF
    db "CO", 0, 0    ; 9  #CO
    db "TS", 0, 0    ; 10 #TS
    db "NP", 0, 0    ; 11 #NP
    db "SS", 0, 0    ; 12 #SS
    db "GP", 0, 0    ; 13 #GP
    db "PF", 0, 0    ; 14 #PF
    db "RS", 0, 0    ; 15 Reserved
    db "MF", 0, 0    ; 16 #MF
    db "AC", 0, 0    ; 17 #AC
    db "MC", 0, 0    ; 18 #MC
    db "XM", 0, 0    ; 19 #XM
    db "VE", 0, 0    ; 20 #VE
    db "CP", 0, 0    ; 21 #CP
    db "??", 0, 0    ; 22
    db "??", 0, 0    ; 23
    db "??", 0, 0    ; 24
    db "??", 0, 0    ; 25
    db "??", 0, 0    ; 26
    db "??", 0, 0    ; 27
    db "??", 0, 0    ; 28
    db "??", 0, 0    ; 29
    db "??", 0, 0    ; 30
    db "??", 0, 0    ; 31
