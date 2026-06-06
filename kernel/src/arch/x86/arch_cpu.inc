; =============================================================================
; arch/x86/arch_cpu.inc — x86 CPU control implementation
; =============================================================================
; This file implements the HAL CPU control interface using x86 instructions.
; For ARM, arch/arm/arch_cpu.inc would use cpsid/cpsie, mrs/msr, wfi.
;
; USAGE: This file is %included by hal_cpu.inc after architecture selection.
; =============================================================================


; --- Interrupt control ---------------------------------------------------

%macro HAL_CPU_DISABLE_INTS 0
    cli
%endmacro

%macro HAL_CPU_ENABLE_INTS 0
    sti
%endmacro

%macro HAL_CPU_SAVE_INT_STATE 0
    pushfd
    cli
%endmacro

%macro HAL_CPU_RESTORE_INT_STATE 0
    popfd                          ; Restores interrupt flag
%endmacro


; --- Descriptor table loads -----------------------------------------------

%macro HAL_CPU_LOAD_GDT 1
    lgdt [%1]
%endmacro

%macro HAL_CPU_LOAD_IDT 1
    lidt [%1]
%endmacro

%macro HAL_CPU_LOAD_TSS 1
    ltr %1
%endmacro


; --- Control registers ---------------------------------------------------

%macro HAL_CPU_READ_CR 2
    %if %1 == 0
        mov %2, cr0
    %elif %1 == 2
        mov %2, cr2
    %elif %1 == 3
        mov %2, cr3
    %elif %1 == 4
        mov %2, cr4
    %else
        %error "HAL_CPU_READ_CR: cr_num must be 0, 2, 3, or 4 (x86)"
    %endif
%endmacro

%macro HAL_CPU_WRITE_CR 2
    %if %1 == 0
        mov cr0, %2
    %elif %1 == 3
        mov cr3, %2
    %elif %1 == 4
        mov cr4, %2
    %else
        %error "HAL_CPU_WRITE_CR: cr_num must be 0, 3, or 4 (x86)"
    %endif
%endmacro


; --- Cache/TLB control -------------------------------------------------

%macro HAL_CPU_INVLPG 1
    invlpg [%1]
%endmacro

%macro HAL_CPU_FLUSH_CACHE 0
    ; x86: wbinvd (write-back and invalidate cache)
    ; Not needed for basic operation, but useful for DMA
    wbinvd
%endmacro

%macro HAL_CPU_FLUSH_TLB 0
    ; x86: rewriting CR3 flushes entire TLB
    mov eax, cr3
    mov cr3, eax
%endmacro


; --- Halt/wait ---------------------------------------------------------

%macro HAL_CPU_HALT 0
    hlt
%endmacro

%macro HAL_CPU_WAIT_FOR_INTR 0
    ; x86: hlt waits for interrupt
    hlt
%endmacro
