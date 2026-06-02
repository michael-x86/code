; calc — simple integer calculator
;   usage:  calc <num1> <op> <num2>   OR   calc <num1><op><num2>
;   Supports: + - * / %  (32-bit unsigned, decimal input)
;   Examples: calc 2 + 2   calc 2+2   calc 100/3   calc 15 % 4
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- Fetch argv[1] (first argument after "calc") ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; --- Check if we have 3 separate args or 1 combined expression ---
    mov ebx, 2
    lea edi, [ebp + op_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80

    cmp eax, -1
    je .single_expr         ; only 1 arg: parse "2+2" from arg_buf

    ; --- 3-arg path: arg1=number, arg2=operator, arg3=number ---
    ; op_buf has the operator, fetch arg3
    mov ebx, 3
    lea edi, [ebp + arg_buf2]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; Parse first number from arg_buf
    lea esi, [ebp + arg_buf]
    mov eax, 27             ; sys_asc2int
    int 0x80
    mov [ebp + val_a], eax

    ; Parse second number from arg_buf2
    lea esi, [ebp + arg_buf2]
    mov eax, 27             ; sys_asc2int
    int 0x80
    mov [ebp + val_b], eax

    ; Read operator from op_buf
    mov al, [ebp + op_buf]
    jmp .dispatch

.single_expr:
    ; --- Parse combined expression like "2+2" from arg_buf ---
    ; Scan for the operator: first non-digit character in the string
    lea esi, [ebp + arg_buf]

.find_op:
    lodsb
    cmp al, '0'
    jb .found_op            ; below '0' — must be operator (or end)
    cmp al, '9'
    ja .found_op            ; above '9' — must be operator
    jmp .find_op            ; digit, keep scanning

.found_op:
    ; al = operator, esi points to the byte AFTER the operator
    mov [ebp + op], al      ; save operator

    ; Null-terminate the first number (replace operator with 0)
    mov byte [esi - 1], 0

    ; Parse first number from arg_buf
    push esi                ; save pointer to second number
    lea esi, [ebp + arg_buf]
    mov eax, 27             ; sys_asc2int
    int 0x80
    mov [ebp + val_a], eax

    ; Parse second number (esi already points past the operator)
    pop esi
    mov eax, 27             ; sys_asc2int
    int 0x80
    mov [ebp + val_b], eax

    mov al, [ebp + op]      ; restore operator for dispatch

.dispatch:
    ; --- Dispatch on operator ---
    mov edx, [ebp + val_a]
    mov ecx, [ebp + val_b]

    cmp al, '+'
    je .do_add
    cmp al, '-'
    je .do_sub
    cmp al, '*'
    je .do_mul
    cmp al, '/'
    je .do_div
    cmp al, '%'
    je .do_mod

    jmp .usage

.do_add:
    mov eax, edx
    add eax, ecx
    jmp .print_result

.do_sub:
    mov eax, edx
    sub eax, ecx
    jmp .print_result

.do_mul:
    mov eax, edx
    mul ecx                 ; edx:eax = eax * ecx (we assume result fits 32-bit)
    jmp .print_result

.do_div:
    test ecx, ecx
    jz .div_zero
    xor edx, edx
    mov eax, [ebp + val_a]
    div ecx                 ; eax = quotient
    jmp .print_result

.do_mod:
    test ecx, ecx
    jz .div_zero
    xor edx, edx
    mov eax, [ebp + val_a]
    div ecx                 ; edx = remainder
    mov eax, edx
    jmp .print_result

.div_zero:
    lea esi, [ebp + divzero_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

.print_result:
    ; Print result as decimal using sys_print_int (syscall 6)
    mov ebx, eax
    mov eax, 6              ; sys_print_int
    int 0x80

    ; Print newline
    mov eax, 3              ; sys_newline
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

usage_msg:   db "usage: calc <num1><op><num2>  or  calc <num1> <op> <num2>", 13, 0
divzero_msg: db "calc: division by zero", 13, 0

section .bss
alignb 4
arg_buf:    resb 32
arg_buf2:   resb 32
op_buf:     resb 8
val_a:      resd 1
val_b:      resd 1
op:         resb 1
