; =============================================================================
; irq.inc — default ISR, page fault, IRQ0 (timer), IRQ1 (keyboard)
; =============================================================================
;
; This file owns the four interrupt handlers that are not the syscall
; entry point (which lives in syscall.inc). Two of them are CPU
; exceptions we expect (page fault) or want to ignore; the other two
; are the hardware IRQs we actually service.
;
; The IRQ0 handler is also where cooperative multitasking happens: the
; PIT fires 100 times a second, and on each fire we save the current
; task's ESP, pick the next task, load its ESP, and iret. The disk_busy
; flag (set by ata_read/ata_write) prevents a context switch while a
; PIO transfer is in progress — switching mid-transfer can let the
; DRQ deadline expire.
;
; set_irq0 / set_irq1 are small helpers that install the gate
; descriptors for IRQ0 and IRQ1 into the IDT. They are called from
; kernel_main, after the IDT is built.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; isr_default — handler for unexpected interrupts and non-exception vectors
; This is used for vectors 32-255 (IRQs and user-defined interrupts).
; Just acknowledge the PIC(s) and return.
; -----------------------------------------------------------------------------
isr_default:
    pushad
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd


; -----------------------------------------------------------------------------
; irq0 — PIT timer tick, also where the round-robin task switch happens
;
; Three things happen on every tick:
;   1. tick_count is incremented. The top-right of the screen shows
;      it as 8 hex digits.
;   2. Every other tick, tick_flag is set. The shell's task 2 reads
;      this and re-renders the tick display — but it doesn't need to
;      do so on every tick, hence the "every other" gate.
;   3. If disk_busy is zero, we context-switch to the next task.
;      disk_busy is set by ata_read/ata_write because the PIO protocol
;      can time out its DRQ window if we switch tasks mid-transfer.
;
; The actual tick display is written DIRECTLY in this ISR (not via
; task 2) so it stays live during long syscalls like disk I/O that
; block task 2's main loop.
; -----------------------------------------------------------------------------
irq0:
    pushad
    inc dword [tick_count]

    inc dword [tick_div]
    test dword [tick_div], 1
    jnz .no_flag
    mov byte [tick_flag], 1
.no_flag:

    ; Live tick display in the top-right corner of the screen.
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
    mov dh, VGA_COLOR_BANNER
    mov [edi], dx
    shl eax, 4
    add edi, 2
    dec ecx
    jnz .tick_disp

    ; Defer task switch if a PIO transfer is in progress.
    cmp byte [disk_busy], 0
    jne .skip_switch

    ; next = (current + 1) % TASK_COUNT
    mov eax, [current_task]
    inc eax
    cmp eax, TASK_COUNT
    jb .save
    xor eax, eax

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

.skip_switch:
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd


; -----------------------------------------------------------------------------
; irq1 — PS/2 keyboard, raw scancode → kbd_buf
;
; Reads scancode from port 0x60, tracks the shift key state, and
; pushes key-press (not key-release) scancodes into the circular
; kbd_buf. The shell's get_key (in input.inc) pulls them out and
; translates via the active keymap.
;
; PS/2 scancode set 1: bit 7 set = key release. We test for the
; known shift make codes (0x2A left, 0x36 right) and their
; counterparts (0xAA, 0xB6) to track the shift state.
;
; Extended keys (prefixed with 0xE0) are handled by setting kbd_extended
; on the prefix and checking the next byte. Page Up (0x49) and Page Down
; (0x51) are buffered as special values 0xF0 and 0xF1 respectively.
; -----------------------------------------------------------------------------
irq1:
    pushad
    in al, 0x60                          ; raw scancode
    mov bl, al

    ; Check for extended key prefix (0xE0).
    cmp bl, 0xE0
    jne .not_extended
    mov byte [kbd_extended], 1
    jmp .done
.not_extended:

    ; If we're expecting an extended key, handle it.
    cmp byte [kbd_extended], 0
    je .normal_key
    mov byte [kbd_extended], 0            ; clear the flag

    ; Ignore extended key releases (bit 7 set).
    test bl, 0x80
    jnz .done

    ; Check for Page Up (0x49) and Page Down (0x51).
    cmp bl, 0x49
    jne .check_pgdn
    mov al, 0xF0                          ; special value for Page Up
    jmp .buffer_extended
.check_pgdn:
    cmp bl, 0x51
    jne .done                             ; ignore other extended keys
    mov al, 0xF1                          ; special value for Page Down
.buffer_extended:
    ; Buffer al (0xF0 or 0xF1) directly.
    mov edi, [kbd_head]
    mov [kbd_buf + edi], al
    inc edi
    and edi, KBD_BUF_SIZE - 1
    mov [kbd_head], edi
    jmp .done

.normal_key:
    cmp bl, 0x2A
    je .shift_on
    cmp bl, 0x36
    je .shift_on
    cmp bl, 0xAA
    je .shift_off
    cmp bl, 0xB6
    je .shift_off
    test bl, 0x80
    jnz .done                            ; ignore all other key releases

    ; Buffer the key-press scancode in the circular kbd_buf.
    mov edi, [kbd_head]
    mov [kbd_buf + edi], bl
    inc edi
    and edi, KBD_BUF_SIZE - 1
    mov [kbd_head], edi
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


; -----------------------------------------------------------------------------
; set_irq0 — install the IRQ0 gate at IDT vector 0x20
; -----------------------------------------------------------------------------
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


; -----------------------------------------------------------------------------
; set_irq1 — install the IRQ1 gate at IDT vector 0x21
; -----------------------------------------------------------------------------
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


; -----------------------------------------------------------------------------
; print_tick — render tick_count at the top-right of the VGA buffer
;
; A re-entrant version of the in-ISR tick display. Called from the
; shell's task 2 when it wants to refresh the tick (it does this on
; every other PIT tick, gated by tick_flag).
; -----------------------------------------------------------------------------
print_tick:
    pushad
    mov eax, [tick_count]
    mov edi, VGA_TEXT_BUFFER + (0 * VGA_COLS + 72) * 2
    mov ecx, 8
.next:
    mov edx, eax
    shr edx, 28
    and dl, 0x0F
    add dl, '0'
    cmp dl, '9'
    jbe .store
    add dl, 7
.store:
    mov dh, VGA_COLOR_BANNER
    mov [edi], dx
    shl eax, 4
    add edi, 2
    dec ecx
    jnz .next
    popad
    ret
