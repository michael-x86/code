; =============================================================================
; task.inc — multitasking: task init, stacks, and entry points
; =============================================================================
;
; Three round-robin tasks scheduled by IRQ0 (PIT timer at 100 Hz):
;   Task 0 — idle loop (hlt)
;   Task 1 — idle loop (hlt)
;   Task 2 — shell / command processor
; =============================================================================

; -----------------------------------------------------------------------------
; init_tasks — set up the initial stack frames for all three tasks
;   Each stack is built top-down: EFLAGS, CS, EIP, then 8 zeroed GPRs.
; -----------------------------------------------------------------------------
init_tasks:
    ; --- Task 0 ---
    mov eax, task0_stack_top
    sub eax, 4
    mov dword [eax], 0x202          ; EFLAGS: IF set
    sub eax, 4
    mov dword [eax], CODE_SEG
    sub eax, 4
    mov dword [eax], task0_entry
    sub eax, 32                      ; 8 registers × 4 bytes
    mov edi, eax
    mov ecx, 8
    xor ebx, ebx
.clear0:
    mov [edi], ebx
    add edi, 4
    loop .clear0
    mov [task0_esp], eax

    ; --- Task 1 ---
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

    ; --- Task 2 (shell) ---
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

; ── Task entry points ──────────────────────────────────────────────────────

; Task 0 — idle
task0_entry:
.loop:
    hlt
    jmp .loop

; Task 1 — idle
task1_entry:
.loop:
    hlt
    jmp .loop

; Task 2 — shell: read keys, dispatch commands
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
    ; Echo character and append to command buffer
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
    hlt
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
    hlt
    jmp task2_entry
