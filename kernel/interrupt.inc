; =============================================================================
; interrupt.inc — IDT setup, IRQ handlers, PIC remapping, PIT frequency
; =============================================================================

; ── IDT ─────────────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; build_idt — fill all 256 IDT entries with the default handler, then
;             install the page-fault handler at vector 14.
; -----------------------------------------------------------------------------
build_idt:
    mov edi, idt_start
    mov ecx, IDT_ENTRIES
.fill:
    mov eax, isr_default
    mov [edi], ax             ; offset low
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0     ; reserved
    mov byte [edi + 5], IDT_PRESENT
    shr eax, 16
    mov [edi + 6], ax         ; offset high
    add edi, 8
    loop .fill

    ; Install page-fault handler (vector 14)
    mov eax, page_fault_isr
    mov ebx, 14
    mov edi, idt_start
    call set_idt_entry
    ret

; -----------------------------------------------------------------------------
; set_idt_entry — write a single IDT gate descriptor
;   in: eax = handler address, ebx = vector index, edi = IDT base
; -----------------------------------------------------------------------------
set_idt_entry:
    mov edx, eax
    mov word [edi + ebx * 8 + 0], dx       ; offset low
    mov word [edi + ebx * 8 + 2], CODE_SEG  ; selector
    mov byte [edi + ebx * 8 + 4], 0
    mov byte [edi + ebx * 8 + 5], IDT_PRESENT
    shr edx, 16
    mov word [edi + ebx * 8 + 6], dx       ; offset high
    ret

; ── Default / spurious handler ──────────────────────────────────────────────

isr_default:
    pushad
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd

; ── Page fault ──────────────────────────────────────────────────────────────

page_fault_isr:
    cli
    pushad
    mov eax, [esp + 36]      ; saved EIP
    push eax
    call page_fault_handler
    add esp, 4
.hang:
    hlt
    jmp .hang

page_fault_handler:
    push ebp
    mov ebp, esp
    PRINT_STRING_CR pf_msg
    PRINT_STRING pf_addr
    ; mov eax, cr2            ; faulting address (uncomment to use)
    ; call print_hex_dword
    call newline

    ; Spin loop so the user can read the message before the screen clears
    mov edx, 0x7777
.outer:
    mov ecx, 0xFFFF
.inner:
    dec ecx
    jnz .inner
    dec edx
    jnz .outer

    pop ebp
    iret

; ── IRQ 0 — PIT timer (task switch) ────────────────────────────────────────
; Round-robin across 3 tasks. Sets tick_flag every other tick for the shell.

irq0:
    pushad
    inc dword [tick_count]

    inc dword [tick_div]
    test dword [tick_div], 1
    jnz .no_flag
    mov byte [tick_flag], 1
.no_flag:

    ; Update tick display directly in IRQ0 so the counter stays live
    ; during long syscalls (disk I/O) that block task2's main loop.
    mov eax, [tick_count]
    mov edi, VGA_TEXT_BUFFER + (0 * VGA_COLS + 72) * 2
    mov ecx, 8
.tick_disp:
    mov edx, eax
    shr edx, 28
    and dl, 0x0F
    add dl, '0'
    cmp dl, '9'
    jbe .tick_store
    add dl, 7
.tick_store:
    mov dh, 0x07
    mov [edi], dx
    shl eax, 4
    add edi, 2
    dec ecx
    jnz .tick_disp

    ; Compute next task index: (current + 1) % 3
    mov eax, [current_task]
    inc eax
    cmp eax, TASK_COUNT
    jb .save
    xor eax, eax

    ; Save current task stack pointer
.save:
    cmp dword [current_task], 0
    je .was0
    cmp dword [current_task], 1
    je .was1
    mov [task2_esp], esp
    jmp .load
.was0:
    mov [task0_esp], esp
    jmp .load
.was1:
    mov [task1_esp], esp

    ; Restore next task stack pointer
.load:
    mov [current_task], eax
    cmp eax, 0
    je .run0
    cmp eax, 1
    je .run1
    mov esp, [task2_esp]
    jmp .done
.run0:
    mov esp, [task0_esp]
    jmp .done
.run1:
    mov esp, [task1_esp]
.done:
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd

; ── IRQ 1 — Keyboard ───────────────────────────────────────────────────────
; Reads scancode from port 0x60, handles shift key state, buffers key presses.

irq1:
    pushad
    in al, 0x60              ; read scancode
    mov bl, al

    ; Shift press
    cmp bl, 0x2A
    je .shift_on
    cmp bl, 0x36
    je .shift_on
    ; Shift release
    cmp bl, 0xAA
    je .shift_off
    cmp bl, 0xB6
    je .shift_off
    ; Ignore all key releases
    test bl, 0x80
    jnz .done

    ; Buffer key press
    mov eax, [kbd_head]
    mov [kbd_buf + eax], bl
    inc eax
    and eax, KBD_BUF_SIZE - 1
    mov [kbd_head], eax
    jmp .done
.shift_on:
    mov byte [kbd_shift], 1
    jmp .done
.shift_off:
    mov byte [kbd_shift], 0
.done:
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd

; ── PIC remap ───────────────────────────────────────────────────────────────
; Remap master PIC to vectors 0x20..0x27, slave to 0x28..0x2F.
; Unmask IRQ0 (timer) and IRQ1 (keyboard); mask everything else.

pic_remap:
    mov al, PIC_INIT
    out PIC_MASTER_CMD, al
    out PIC_SLAVE_CMD, al
    mov al, PIC_MASTER_OFFSET
    out PIC_MASTER_DATA, al
    out PIC_SLAVE_DATA, al   ; slave offset
    mov al, 0x04             ; master: slave at IRQ2
    out PIC_MASTER_DATA, al
    mov al, 0x02             ; slave cascade identity
    out PIC_SLAVE_DATA, al
    mov al, 0x01             ; 8086 mode
    out PIC_MASTER_DATA, al
    out PIC_SLAVE_DATA, al
    mov al, 0b11111100       ; unmask IRQ0 + IRQ1 only
    out PIC_MASTER_DATA, al
    mov al, 0b11111111       ; mask all slave IRQs
    out PIC_SLAVE_DATA, al
    ret

; ── PIT frequency ───────────────────────────────────────────────────────────
; Program the PIT channel 0 to PIT_FREQUENCY Hz.

set_freq:
    ; divisor = 1193182 / frequency
    mov al, 0x36             ; channel 0, lobyte/hibyte, square wave
    out 0x43, al
    mov ax, PIT_DIVISOR
    out 0x40, al             ; low byte
    mov al, ah
    out 0x40, al             ; high byte
    ret

; ── IDT entry points (installed after IDT is built) ─────────────────────────

set_irq0:
    mov eax, irq0
    mov edx, eax
    mov edi, idt_start + 0x20 * 8
    mov word [edi], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], IDT_PRESENT
    shr edx, 16
    mov word [edi + 6], dx
    ret

set_irq1:
    mov eax, irq1
    mov edi, idt_start + 0x21 * 8
    mov word [edi], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], IDT_PRESENT
    shr eax, 16
    mov word [edi + 6], ax
    ret
