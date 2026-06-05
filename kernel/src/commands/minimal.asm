; minimal.asm - Absolute minimal test
; Just prints 'K' and loops - no ebp, no strings

[bits 32]
[org 0x00000000]

global _start
_start:
    ; Print 'K' - use syscall 0 (PUTCHAR)
    ; eax = syscall number, ebx = char
    mov eax, 0          ; SYS_PUTCHAR
    mov ebx, 'K'
    int 0x80
    
    ; Print newline
    mov eax, 3          ; SYS_NEWLINE
    int 0x80
    
    ; Loop forever so we know program started
.loop:
    jmp .loop
