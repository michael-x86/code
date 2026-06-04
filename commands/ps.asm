[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

    ; --- Get kernel task info ---
    lea ebx, [ebp + ps_buffer]
    mov eax, 22                 ; sys_get_ps_info
    int 0x80
    cmp eax, -1
    je .done

    ; --- Get userland process info ---
    lea ebx, [ebp + proc_buffer]
    mov eax, 28                 ; sys_get_proc_info
    int 0x80

    ; --- Print header ---
    lea esi, [ebp + hdr_msg]
    mov eax, 2                  ; sys_print_cr
    int 0x80

    ; --- Task 0: idle ---
    lea esi, [ebp + task0_lbl]
    mov eax, 2
    int 0x80
    mov ebx, [ebp + ps_buffer + 4]   ; task0 ESP
    mov eax, 5                       ; sys_print_hex
    int 0x80
    lea esi, [ebp + idle_lbl]
    mov eax, 2
    int 0x80

    ; --- Task 1: idle ---
    lea esi, [ebp + task1_lbl]
    mov eax, 2
    int 0x80
    mov ebx, [ebp + ps_buffer + 8]   ; task1 ESP
    mov eax, 5
    int 0x80
    mov ecx, 1
    call print_task_state

    ; --- Task 2: shell ---
    lea esi, [ebp + task2_lbl]
    mov eax, 2
    int 0x80
    mov ebx, [ebp + ps_buffer + 12]  ; task2 ESP
    mov eax, 5
    int 0x80
    mov ecx, 2
    call print_task_state

    ; --- Userland process (if any) ---
    mov eax, [ebp + proc_buffer]     ; exec_vbase
    test eax, eax
    jz .done                         ; no userland process active

    ; Print userland entry: PID 3, name from proc_buffer+8
    lea esi, [ebp + usr_pid]
    mov eax, 2
    int 0x80

    ; Print name (32 bytes at proc_buffer+8)
    lea esi, [ebp + proc_buffer + 8]
    mov eax, 2
    int 0x80

    ; Pad with spaces to align ADDRESS column
    lea esi, [ebp + usr_pad]
    mov eax, 2
    int 0x80

    ; Print load address
    mov ebx, [ebp + proc_buffer]     ; vbase
    mov eax, 5
    int 0x80

    ; State: RUNNING if current_task==2, else SLEEPING
    mov edx, [ebp + ps_buffer]       ; current_task
    cmp edx, 2
    je .usr_running
    lea esi, [ebp + sleeping_lbl]
    mov eax, 2
    int 0x80
    jmp .done
.usr_running:
    lea esi, [ebp + running_lbl]
    mov eax, 2
    int 0x80

.done:
    ret

; --- print_task_state: ecx = task index to check ---
print_task_state:
    push edx
    mov edx, [ebp + ps_buffer]       ; current_task
    cmp edx, ecx
    je .running
    lea esi, [ebp + sleeping_lbl]
    mov eax, 2
    int 0x80
    pop edx
    ret
.running:
    lea esi, [ebp + running_lbl]
    mov eax, 2
    int 0x80
    pop edx
    ret

; --- Strings ---
hdr_msg:      db "PID   TASK  NAME          ADDRESS      STATE", 13, 0
task0_lbl:    db "root  000   idle          0x", 0
task1_lbl:    db "root  001   idle          0x", 0
task2_lbl:    db "root  002   shell         0x", 0
usr_pid:      db "root  003   ", 0
usr_pad:      db "               ", 0
idle_lbl:     db "   IDLE", 13, 0
sleeping_lbl: db "   SLEEPING", 13, 0
running_lbl:  db "   RUNNING", 13, 0

section .bss
alignb 4
ps_buffer:    resd 4                 ; 16 bytes: current_task, task0-2 ESP
proc_buffer:  resb 36                ; 4 vbase + 4 pages + 32 name
