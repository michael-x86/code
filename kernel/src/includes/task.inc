; =============================================================================
; task.inc — task stack initialization and entry points
; =============================================================================
;
; We have three round-robin tasks, scheduled by IRQ0 (the PIT timer):
;--
;   task0  idle (sti; hlt in a loop)
;   task1  idle (sti; hlt in a loop)
;   task2  shell  (read keys, dispatch commands)
;
; Each task gets its own 4 KB stack. The first context switch is special:
;-- the kernel entry code does `mov esp, [task0_esp]; popad; iretd` to
;-- jump into task 0. After that, IRQ0 saves the current ESP into the
;-- outgoing task's slot, picks the next task, and iret's into it.
;
; We hand-craft the initial iret frame so the very first switch is just
;-- a popad + iret. The frame layout is the one iret expects, top-down:
;--
;   [esp+0]  EAX   (zeroed)
;   [esp+4]  ECX   (zeroed)
;   [esp+8]  EDX   (zeroed)
;   [esp+12] EBX   (zeroed)
;   [esp+16] ESP   (zeroed; iret ignores)
;   [esp+20] EBP   (zeroed)
;   [esp+24] ESI   (zeroed)
;   [esp+28] EDI   (zeroed)
;   [esp+32] EIP   (entry point of the task)
;--   [esp+36] CS    (0x08, kernel code segment)
;--   [esp+40] EFLAGS (0x202, IF set so the task can be interrupted)
;
; popad pops 8 dwords into the GPRs (skipping the slot iret writes
;-- its result ESP to). Then iret pops EIP, CS, EFLAGS in that order
;-- and transfers control to the task.
; =============================================================================

%include "hal/hal.inc"

section .text
section .text


; -----------------------------------------------------------------------------
; init_tasks — build the initial iret frame for each task
; -----------------------------------------------------------------------------
init_tasks:
    ; --- Task 0 ----------------------------------------------------------
    mov eax, task0_stack_top
    sub eax, 4
    mov dword [eax], 0x202               ; EFLAGS: IF=1 so IRQ0 can preempt
    sub eax, 4
    mov dword [eax], CODE_SEG            ; CS
    sub eax, 4
    mov dword [eax], task0_entry         ; EIP
    sub eax, 32                         ; 8 zeroed GPRs
    mov edi, eax
    mov ecx, 8
    xor ebx, ebx
.clear0:
    mov [edi], ebx
    add edi, 4
    loop .clear0
    mov [task0_esp], eax                 ; ESP = first dword of the zeroed region

    ; --- Task 1 ----------------------------------------------------------
    mov eax, task1_stack_top
    sub eax, 4
    mov dword [eax], 0x202
    sub eax, 4
    mov dword [eax], CODE_SEG
    sub eax, 4
    mov dword [eax], task1_entry
    sub eax, 32
    mov edi, eax
    mov ecx, 8
    xor ebx, ebx
.clear1:
    mov [edi], ebx
    add edi, 4
    loop .clear1
    mov [task1_esp], eax

    ; --- Task 2 (shell) --------------------------------------------------
    mov eax, task2_stack_top
    sub eax, 4
    mov dword [eax], 0x202
    sub eax, 4
    mov dword [eax], CODE_SEG
    sub eax, 4
    mov dword [eax], task2_entry
    sub eax, 32
    mov edi, eax
    mov ecx, 8
    xor ebx, ebx
.clear2:
    mov [edi], ebx
    add edi, 4
    loop .clear2
    mov [task2_esp], eax
    ret


; -----------------------------------------------------------------------------
; Task 0 — idle
; -----------------------------------------------------------------------------
task0_entry:
.loop:
    HAL_ENABLE_INTS
    HAL_HALT
    jmp .loop


; -----------------------------------------------------------------------------
; Task 1 — idle
; -----------------------------------------------------------------------------
task1_entry:
.loop:
    HAL_ENABLE_INTS
    HAL_HALT
    jmp .loop


; -----------------------------------------------------------------------------
; Task 2 — shell
;--
;-- Main loop: poll get_key. On keypress, dispatch by character:
;--   - CR (Enter) → save history, parse args, dispatch_command, redraw prompt
;--   - BS (backspace) → delchar, decrement cmd_len
;--   - TAB → tab_complete (the candidate list is shown or the line
;--     is auto-completed in place)
;--   - anything else → echo + append to cmd_buf
;--
;-- Between keys we also re-render the live tick display if tick_flag
;-- is set, so the top-right counter stays current even when the user
;-- is idle at a prompt.
; -----------------------------------------------------------------------------
task2_entry:
    cmp byte [tick_flag], 0
    je .skip
    mov byte [tick_flag], 0
    call print_tick
.skip:
    call get_key
    test al, al
    jz .done
    cmp al, 13
    je .enter
    cmp al, 8
    je .del
    cmp al, 9
    je .tab
    ; Echo and append to the command buffer.
    call putchar
    mov ebx, [cmd_len]
    cmp ebx, CMD_BUF_SIZE - 1
    jae .done
    mov [cmd_buf + ebx], al
    inc ebx
    mov [cmd_len], ebx
    jmp .done
.enter:
    mov ebx, [cmd_len]
    mov byte [cmd_buf + ebx], 0
    call newline
    call save_history
    call parse_args
    call dispatch_command
    mov dword [cmd_len], 0
    mov byte [cmd_buf], 0
    call prompt
    HAL_HALT
    jmp task2_entry
.tab:
    call tab_complete
    xor al, al
    jmp .done
.del:
    cmp dword [cmd_len], 0
    jbe .done
    dec dword [cmd_len]
    call delchar
.done:
    HAL_ENABLE_INTS
    HAL_HALT
    jmp task2_entry
