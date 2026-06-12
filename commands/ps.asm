[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- sys_get_ps_info ---
    lea ebx, [ebp + ps_buffer]  
    mov eax, 22                 
    int 0x80
    
    ; --- System Header ---
    lea esi, [ebp + hdr_msg]
    mov eax, 2                  
    int 0x80

    ; --- Loop exactly 4 times (Tasks 0, 1, 2, and 3) ---
    mov ecx, 4                  
    xor edi, edi                ; edi = current task index (0 to 3)

.task_loop:
    push ecx                    ; Preserve loop counter

    ; 1. Resolve Text Labels (Using near to prevent out-of-range errors)
    cmp edi, 0
    je near .lbl0
    cmp edi, 1
    je near .lbl1
    cmp edi, 2
    je near .lbl2
    
    ; If it isn't 0, 1, or 2, it must be task 3 (ps / active command)
    lea esi, [ebp + pstsk_lbl]
    jmp near .print_lbl
.lbl0:
    lea esi, [ebp + task0_lbl]
    jmp near .print_lbl
.lbl1:
    lea esi, [ebp + task1_lbl]
    jmp near .print_lbl
.lbl2:
    lea esi, [ebp + task2_lbl]

.print_lbl:
    mov eax, 2                  
    int 0x80

    ; 2. Print Task Memory Pointer
    ; Offset by +4 because ps_buffer+0 holds the active task ID flag
    mov ebx, [ebp + ps_buffer + 4 + edi * 4]
    mov eax, 5                  
    int 0x80

    ; 3. Print State (IDLE, RUNNING, or SLEEPING)
    cmp edi, 0
    je near .print_idle         ; Task 0 is always architecturally IDLE
    
    call print_user_state
    jmp near .next_task

.print_idle:
    lea esi, [ebp + idle_lbl]
    mov eax, 2
    int 0x80

.next_task:
    inc edi                     ; Next task index
    pop ecx                     ; Restore loop counter
    loop .task_loop             

.done:
    ; --- Graceful Exit via int 0x80 ---
    mov ebx, 0                  
    mov eax, 0                  
    int 0x80

print_user_state:
    ; Read the running task ID flag out of ps_buffer+0
    mov edx, [ebp + ps_buffer + 0] 
    cmp edx, edi                ; Is this task index currently running?
    je near .running
    lea esi, [ebp + sleeping_lbl]
    mov eax, 2
    int 0x80
    ret
.running:
    lea esi, [ebp + running_lbl]
    mov eax, 2
    int 0x80
    ret

; --- Data Section ---
hdr_msg:      db "PID   TASK  NAME           ADDRESS      STATE", 13, 0
task0_lbl:    db "root  000   skynet -d      0x", 0
task1_lbl:    db "root  001   [hal-9000]     0x", 0
task2_lbl:    db "root  002   /sbin/init     0x", 0
pstsk_lbl:    db "root  003   ps             0x", 0 

idle_lbl:     db "   IDLE", 13, 0
sleeping_lbl: db "   SLEEPING", 13, 0
running_lbl:  db "   RUNNING", 13, 0

section .bss
alignb 4
ps_buffer:    resd 6  ; matching 5 core entries
