; =============================================================================
; userland.inc — Standard startup and syscall wrappers for userland programs
; =============================================================================
;
; This library consolidates boilerplate code that was previously duplicated
; across 21+ userland programs. Include it with:
;
;   %include "userland.inc"
;
; Then use the provided macros and functions to reduce code duplication.
;
; All macros preserve registers where appropriate. Syscall wrappers follow
; the kernel's syscall convention (eax = syscall number, int 0x80).
;
; =============================================================================

; -----------------------------------------------------------------------------
; USERLAND_START — Standard position-independent startup boilerplate
; -----------------------------------------------------------------------------
; This macro expands to the identical 3-instruction sequence that every
; userland program currently has at _start. It calculates EBP as the
; base address for position-independent addressing of labels.
;
; Usage:
;   _start:
;       USERLAND_START
;       ; ... your code here ...
; -----------------------------------------------------------------------------
%macro USERLAND_START 0
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base
%endmacro

; -----------------------------------------------------------------------------
; Syscall wrapper macros — inline syscall invocation
; -----------------------------------------------------------------------------
; These macros set up the syscall number in EAX and invoke int 0x80.
; They don't preserve registers (matching current inline patterns).
;
; Usage:
;   mov ebx, 1              ; set up arguments
;   lea edi, [ebp + buffer]
;   SYS_GET_ARG              ; instead of: mov eax, 14 / int 0x80
; -----------------------------------------------------------------------------

%macro SYS_GET_ARG 0
    mov eax, 14
    int 0x80
%endmacro

%macro SYS_PRINT 0
    mov eax, 1
    int 0x80
%endmacro

%macro SYS_PRINT_CR 0
    mov eax, 2
    int 0x80
%endmacro

%macro SYS_NEWLINE 0
    mov eax, 3
    int 0x80
%endmacro

%macro SYS_PRINT_HEX 0
    mov eax, 5
    int 0x80
%endmacro

%macro SYS_ASC2INT 0
    mov eax, 27
    int 0x80
%endmacro

%macro SYS_STAT 0
    mov eax, 15
    int 0x80
%endmacro

%macro SYS_PRINT_N 0
    mov eax, 16
    int 0x80
%endmacro

; -----------------------------------------------------------------------------
; Error and usage message macros
; -----------------------------------------------------------------------------
; Standardized error handling to replace duplicated patterns across programs.
;
; Usage:
;   .usage:
;       PRINT_USAGE [ebp + usage_msg]
;
;   .not_found:
;       PRINT_ERROR [ebp + nf_msg]
; -----------------------------------------------------------------------------

%macro PRINT_USAGE 1
    lea esi, [ebp + %1]
    SYS_PRINT_CR
    ret
%endmacro

%macro PRINT_ERROR 1
    lea esi, [ebp + %1]
    SYS_PRINT_CR
    ret
%endmacro

%macro PRINT_SUCCESS 1
    lea esi, [ebp + %1]
    SYS_PRINT_CR
    ret
%endmacro

; -----------------------------------------------------------------------------
; Argument parsing helper
; -----------------------------------------------------------------------------
; Common pattern: get argument, check for error, parse if needed
;
; Usage:
;   ; Get first argument
;   GET_ARG 1, arg_buf
;   cmp eax, -1
;   je .usage
;
;   ; Parse as integer
;   lea esi, [ebp + arg_buf]
;   PARSE_INT
;   mov ecx, eax
; -----------------------------------------------------------------------------

%macro GET_ARG 2
    mov ebx, %1
    lea edi, [ebp + %2]
    SYS_GET_ARG
%endmacro

%macro PARSE_INT 0
    SYS_ASC2INT
    mov ecx, eax
%endmacro

; -----------------------------------------------------------------------------
; Standard BSS section template
; -----------------------------------------------------------------------------
; Most programs need an argument buffer. This macro provides a standard
; one that can be customized if needed.
;
; Usage:
;   SECTION_BSS_DEFAULT
;   ; ... or define custom BSS variables ...
; -----------------------------------------------------------------------------

%macro SECTION_BSS_DEFAULT 0
section .bss
alignb 4
arg_buf:    resb 32
%endmacro

; =============================================================================
; End of userland.inc
; =============================================================================
