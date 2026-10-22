; write - write text to a file (one line, trailing newline appended).
;   usage: write [-a] <file> <text...>
;     write <file> <text>       overwrite the file      (sys_write, 18)
;     write -a <file> <text>    append a line to it     (sys_append, 51)
;   The file must already exist (see `touch`). Append lets you build a
;   multi-line file such as exec.cmd for the `run` command.
[bits 32]
[org 0x00000000]

_start:
    call .base
.base:
    pop ebp
    sub ebp, .base

    ; --- arg 1: could be the "-a" flag or the filename ---
    mov ebx, 1
    lea edi, [ebp + arg1]
    mov eax, 14                 ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; default: overwrite, file = arg1, text starts at arg2
    mov dword [ebp + wsys], 18
    mov edx, 1                  ; file arg index
    mov ecx, 2                  ; first text arg index

    ; detect "-a"
    mov al, [ebp + arg1]
    cmp al, '-'
    jne .have_idx
    mov al, [ebp + arg1 + 1]
    cmp al, 'a'
    jne .have_idx
    mov al, [ebp + arg1 + 2]
    test al, al
    jne .have_idx
    mov dword [ebp + wsys], 51  ; append
    mov edx, 2
    mov ecx, 3
.have_idx:
    ; --- read the filename (arg index edx) ---
    push ecx
    mov ebx, edx
    lea edi, [ebp + file_arg]
    mov eax, 14
    int 0x80
    pop ecx
    cmp eax, -1
    je .usage

    ; --- join the remaining args (from index ecx) into text_buf ---
    lea eax, [ebp + text_buf]
    mov [ebp + out_ptr], eax
    mov ebx, ecx               ; ebx = current arg index
    mov dword [ebp + first], 1
.next:
    push ebx
    lea edi, [ebp + word_buf]
    mov eax, 14
    int 0x80
    pop ebx
    cmp eax, -1
    je .done

    cmp dword [ebp + first], 1
    je .skip_sp
    mov edi, [ebp + out_ptr]   ; separating space
    mov byte [edi], ' '
    inc edi
    mov [ebp + out_ptr], edi
.skip_sp:
    mov dword [ebp + first], 0
    lea esi, [ebp + word_buf]
    mov edi, [ebp + out_ptr]
.cp:
    mov al, [esi]
    test al, al
    jz .endw
    mov [edi], al
    inc esi
    inc edi
    jmp .cp
.endw:
    mov [ebp + out_ptr], edi
    inc ebx
    jmp .next

.done:
    ; terminate the line with a newline
    mov edi, [ebp + out_ptr]
    mov byte [edi], 0x0A
    inc edi
    mov ecx, edi
    lea eax, [ebp + text_buf]
    sub ecx, eax               ; ecx = byte count

    lea esi, [ebp + file_arg]
    lea ebx, [ebp + text_buf]
    mov eax, [ebp + wsys]      ; 18 = overwrite, 51 = append
    int 0x80
    cmp eax, -1
    je .err
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret
.err:
    lea esi, [ebp + err_msg]
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: write [-a] <file> <text>", 13, 0
err_msg   db "write: failed", 13, 0

    align 4
_bss      equ $
out_ptr   equ _bss + 0
first     equ _bss + 4
wsys      equ _bss + 8
arg1      equ _bss + 12       ; 64 bytes
file_arg  equ _bss + 76       ; 64 bytes
word_buf  equ _bss + 140      ; 64 bytes
text_buf  equ _bss + 204      ; remainder of the mapped page
