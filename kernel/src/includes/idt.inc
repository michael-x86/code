; =============================================================================
; idt.inc — Interrupt Descriptor Table setup
; =============================================================================
;
; The IDT is a 256-entry table of 8-byte gate descriptors. Each gate
; tells the CPU "if INT n fires, push this state and jump to this
; handler". The CPU pre-fills the gate with the handler's address and
; our flat 4 GB code segment selector.
;
; Most entries in this kernel point at isr_default (in irq.inc) — they
; fire only on CPU exceptions or unexpected IRQs, both of which we
; treat as fatal. The exceptions we actually expect to handle are:
;   14   page fault → page_fault_isr
;   0x20 IRQ0 (PIT) → irq0
;   0x21 IRQ1 (keyboard) → irq1
;   0x80 INT 0x80 (syscall) → syscall_isr (DPL=3 so userland can call it)
;
; idt_start and idt_end are defined in bss.inc. We use idt_descriptor
; (in data.inc) as the operand of lidt.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; build_idt — fill the IDT with default handlers, then install real ones
;
; Every entry starts as an interrupt gate pointing at isr_default with
; DPL=0. The page-fault handler (vector 14) is installed last; it is the
; only CPU exception we currently handle deliberately.
; -----------------------------------------------------------------------------
build_idt:
    mov edi, idt_start
    mov ecx, IDT_ENTRIES
.fill:
    mov eax, isr_default
    mov [edi], ax                        ; offset low (bits 0..15)
    mov word [edi + 2], CODE_SEG         ; segment selector
    mov byte [edi + 4], 0                ; IST index (unused on i386)
    mov byte [edi + 5], IDT_PRESENT      ; gate type/attributes
    shr eax, 16
    mov [edi + 6], ax                    ; offset high (bits 16..31)
    add edi, 8
    loop .fill

    ; Vector 14: page fault
    mov eax, page_fault_isr
    mov ebx, 14
    mov edi, idt_start
    call set_idt_entry
    ret


; -----------------------------------------------------------------------------
; set_idt_entry — write a single 8-byte gate descriptor
;   in:  eax = handler address, ebx = vector index, edi = IDT base
;
; A small refactoring of the gate-encoding logic so build_idt and the
; IRQ/syscall installers don't each repeat the same 6-line dance.
; -----------------------------------------------------------------------------
set_idt_entry:
    mov edx, eax
    mov word [edi + ebx * 8 + 0], dx     ; offset low
    mov word [edi + ebx * 8 + 2], CODE_SEG
    mov byte [edi + ebx * 8 + 4], 0
    mov byte [edi + ebx * 8 + 5], IDT_PRESENT
    shr edx, 16
    mov word [edi + ebx * 8 + 6], dx     ; offset high
    ret
