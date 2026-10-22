[bits 32]
[org 0x00000000]

_start:
    ;pushad
    
    call .get_base
.get_base:
    pop ebp                  
    sub ebp, .get_base    

    mov ebx,1                ; first value after "kill"
    lea edi,[ebp+arg]        ; Target local buffer destination
    mov eax,14               ; sys_get_arg
    int 0x80
    cmp eax,-1               ; no argument exists, fail
    je .usage

    ; point EBX to local argument buffer
    lea ebx,[ebp+arg]

.skip_spaces:
    mov al,[ebx]
    cmp al,' '
    jne .check_empty
    inc ebx
    jmp .skip_spaces

.check_empty:
    test al,al
    jz .usage
    cmp al,13
    je .usage

    ;  PID -> ECX
    xor ecx, ecx            
.parse_loop:
    mov al,[ebx]
    
    test al,al             ; Null terminator
    jz .do_kill
    cmp al,13              ; Carriage return
    je .do_kill
    cmp al,' '             ; Trailing space
    je .do_kill
    
    cmp al,'0'
    jb .usage
    cmp al,'9'
    jg .usage
    
    imul ecx,10
    sub al,'0'
    movzx eax,al
    add ecx,eax
    
    inc ebx
    jmp .parse_loop

.do_kill:
    ; Safety Check??
    cmp ecx, 3
    ;jb .protected_task    ; No!

    mov ebx,ecx            ; ebx = target integer PID
    mov eax,40             ; sys_kill
    int 0x80
    
    cmp eax,-1             ; Check if kernel rejected it
    je .kill_failed

    ; Print success
    lea esi,[ebp+msg_ok]
    mov eax,2              ; sys_print_string
    int 0x80
    jmp .exit

.protected_task:
    lea esi,[ebp+msg_prot]
    mov eax, 2
    int 0x80
    jmp .exit

.kill_failed:
    lea esi,[ebp+msg_fail]
    mov eax,2
    int 0x80
    jmp .exit

.usage:
    lea esi,[ebp+msg_usage]
    mov eax,2
    int 0x80

.exit:
    ;popad
    ret

; -------------------------------------------------------------------------
; Data Area
; -------------------------------------------------------------------------
msg_usage: db "Usage: kill <pid>",13,0
msg_prot:  db "Error: Cannot kill critical system tasks (PID 0-2).",13,0
msg_fail:  db "kill: no such process",13,0
msg_ok:    db "Process terminated successfully.",13,0

section .bss
alignb 4
arg:       resb 32          ; parsed argument string
