; =============================================================================
; arch/arm/arch_cpu.inc — ARM CPU control implementation (AArch32)
; =============================================================================
; This file implements the HAL CPU control interface using ARM instructions.
; For x86, arch/x86/arch_cpu.inc uses x86 instructions (cli, sti, etc.)
;
; TARGET: Raspberry Pi 4 / uConsole CM4 (BCM2711, Cortex-A72, ARMv8-A)
; MODE: AArch32 (32-bit ARM mode, compatible with ARMv7-A)
; ASSEMBLER: GAS (GNU Assembler) or ARMasm (not NASM!)
;
; NOTE: This file uses ARM assembly syntax, NOT NASM syntax.
;       For x86, use arch/x86/arch_cpu.inc (NASM syntax).
;       For ARM port, the build system must use GAS, not NASM.
; =============================================================================


; --- Interrupt control ---------------------------------------------------

; HAL_CPU_DISABLE_INTS — Disable interrupts (IRQ only)
;   ARM: cpsid i   (disable IRQ, leave FIQ enabled)
;   x86 equivalent: cli
;   CPSR.I bit = 1 (IRQ disabled)
.macro HAL_CPU_DISABLE_INTS
    cpsid i                     ; Disable IRQ (ARMv7-A / ARMv8-A AArch32)
.endm

; HAL_CPU_ENABLE_INTS — Enable interrupts (IRQ only)
;   ARM: cpsie i   (enable IRQ)
;   x86 equivalent: sti
;   CPSR.I bit = 0 (IRQ enabled)
.macro HAL_CPU_ENABLE_INTS
    cpsie i                     ; Enable IRQ (ARMv7-A / ARMv8-A AArch32)
.endm

; HAL_CPU_SAVE_INT_STATE — Push CPSR to stack
;   ARM: mrs r0, CPSR  then push {r0}
;   x86 equivalent: pushfd + cli
.macro HAL_CPU_SAVE_INT_STATE
    mrs r0, CPSR               ; Read Current Program Status Register
    push {r0}                  ; Save CPSR on stack
    cpsid i                     ; Disable IRQ (like x86 cli)
.endm

; HAL_CPU_RESTORE_INT_STATE — Pop CPSR from stack
;   ARM: pop {r0}  then  msr CPSR, r0
;   x86 equivalent: popfd
.macro HAL_CPU_RESTORE_INT_STATE
    pop {r0}                   ; Restore saved CPSR
    msr CPSR, r0               ; Write back to CPSR (restores I bit)
.endm


; --- Descriptor table loads -----------------------------------------------

; NOTE: ARM does NOT have GDT/IDT like x86!
;       Instead, ARM uses:
;       - VBAR (Vector Base Address Register) for exception vector table
;       - TTBR0/TTBR1 (Translation Table Base Registers) for MMU
;       These are system registers, accessed via mcr/mrc (or msr/mrs in ARMv8)

; HAL_CPU_LOAD_GDT addr — Set exception vector table base (ARM: VBAR)
;   ARM: mcr p15, 0, r0, c12, c0, 0   (VBAR = addr)
;   x86 equivalent: lgdt [addr]
.macro HAL_CPU_LOAD_GDT addr
    ldr r0, =\addr                 ; Load address into r0
    mcr p15, 0, r0, c12, c0, 0  ; Write to VBAR (Vector Base Address Register)
.endm

; HAL_CPU_LOAD_IDT addr — ARM does NOT have IDT!
;   ARM uses exception vectors at VBAR (set by HAL_CPU_LOAD_GDT)
;   x86 equivalent: lidt [addr]
;   For ARM: This macro is a NO-OP (or set VBAR if not already set)
.macro HAL_CPU_LOAD_IDT addr
    ; ARM does NOT have IDT. Exception vectors are at VBAR.
    ; If needed, set VBAR here (but usually done in HAL_CPU_LOAD_GDT)
    ; NO-OP for ARM (or uncomment below to set VBAR):
    ; ldr r0, =\addr
    ; mcr p15, 0, r0, c12, c0, 0
.endm

; HAL_CPU_LOAD_TSS selector — ARM does NOT have TSS!
;   ARM uses SP_EL0/SP_EL1 (Stack Pointer for EL0/EL1)
;   x86 equivalent: ltr selector
;   For ARM: This macro is a NO-OP (or set SP_EL0 if needed)
.macro HAL_CPU_LOAD_TSS selector
    ; ARM does NOT have TSS. Each exception level has its own SP.
    ; For AArch32: SP (stack pointer) is banked for each mode.
    ; NO-OP for ARM (or set SP_usr if needed):
    ; cps #0x10              ; Switch to User mode (SVC -> USR)
    ; ldr sp, =\selector     ; Set User stack pointer
    ; cps #0x13              ; Switch back to SVC mode
.endm


; --- Control registers ---------------------------------------------------

; HAL_CPU_READ_CR cr_num, reg — Read system register (ARM: mrs)
;   ARM: mrs <reg>, <system_register>
;   x86 equivalent: mov <reg>, cr<num>
;   NOTE: ARM doesn't have "CR0/CR2/CR3" — uses different register names!
.macro HAL_CPU_READ_CR cr_num, reg
    .if \cr_num == 0
        ; ARM: No direct equivalent to CR0 (PE, MP, EM, TS, ET, NE, WP, AM, NW, CD, PG)
        ;      These are in SCTLR_EL1 (System Control Register) for ARMv8
        mrs \reg, SCTLR_EL1        ; Read System Control Register
    .elseif \cr_num == 2
        ; ARM: No direct equivalent to CR2 (Page Fault Linear Address)
        ;      ARM stores fault address in FAR_EL1 (Fault Address Register)
        mrs \reg, FAR_EL1         ; Read Fault Address Register
    .elseif \cr_num == 3
        ; ARM: TTBR0_EL1 (Translation Table Base Register 0)
        mrs \reg, TTBR0_EL1       ; Read TTBR0 (page table base)
    .elseif \cr_num == 4
        ; ARM: No direct equivalent to CR4 (PAE, PGE, PCIDE, etc.)
        ;      These are in SCTLR_EL1 or HCR_EL2
        mrs \reg, SCTLR_EL1        ; Read System Control Register
    .endif
.endm

; HAL_CPU_WRITE_CR cr_num, reg — Write system register (ARM: msr)
;   ARM: msr <system_register>, <reg>
;   x86 equivalent: mov cr<num>, <reg>
.macro HAL_CPU_WRITE_CR cr_num, reg
    .if \cr_num == 0
        ; ARM: SCTLR_EL1 (System Control Register)
        msr SCTLR_EL1, \reg        ; Write System Control Register
    .elseif \cr_num == 3
        ; ARM: TTBR0_EL1 (Translation Table Base Register 0)
        msr TTBR0_EL1, \reg        ; Write TTBR0 (page table base)
        isb                          ; Instruction Synchronization Barrier
    .elseif \cr_num == 4
        ; ARM: SCTLR_EL1 (or HCR_EL2 for virtualization)
        msr SCTLR_EL1, \reg        ; Write System Control Register
    .endif
.endm


; --- Cache/TLB control -------------------------------------------------

; HAL_CPU_INVLPG addr — Invalidate TLB entry for addr (ARM: TLBIMVA)
;   ARM: mcr p15, 0, <addr>, c8, c7, 1   (TLBIMVA - Invalidate TLB by MVA)
;   x86 equivalent: invlpg [addr]
.macro HAL_CPU_INVLPG addr
    ldr r0, =\addr                 ; Load address into r0
    mcr p15, 0, r0, c8, c7, 1  ; TLBIMVA (Invalidate TLB entry by MVA)
    dsb                          ; Data Synchronization Barrier
    isb                          ; Instruction Synchronization Barrier
.endm

; HAL_CPU_FLUSH_CACHE — Flush CPU caches (ARM: DCCISW / DCCIMVAC)
;   ARM: mcr p15, 0, <addr>, c7, c14, 1   (DCCIMVAC - Clean and Invalidate Data Cache line by MVA)
;   x86 equivalent: wbinvd
.macro HAL_CPU_FLUSH_CACHE
    ; Clean and Invalidate all data caches (simplified - actual implementation
    ; would loop through all cache sets/ways)
    ; For simplicity, use DSB + ISB (ensures all cache ops complete)
    dsb                          ; Data Synchronization Barrier
    isb                          ; Instruction Synchronization Barrier
.endm

; HAL_CPU_FLUSH_TLB — Flush entire TLB (ARM: TLBIALL)
;   ARM: mcr p15, 0, r0, c8, c7, 0   (TLBIALL - Invalidate All Unified TLB entries)
;   x86 equivalent: mov eax, cr3; mov cr3, eax
.macro HAL_CPU_FLUSH_TLB
    mov r0, #0                   ; Zero register
    mcr p15, 0, r0, c8, c7, 0  ; TLBIALL (Invalidate All TLB entries)
    dsb                          ; Data Synchronization Barrier
    isb                          ; Instruction Synchronization Barrier
.endm


; --- Halt/wait ---------------------------------------------------------

; HAL_CPU_HALT — Halt CPU (ARM: wfi - Wait For Interrupt)
;   ARM: wfi
;   x86 equivalent: hlt
.macro HAL_CPU_HALT
    wfi                          ; Wait For Interrupt (ARMv7-A / ARMv8-A)
.endm

; HAL_CPU_WAIT_FOR_INTR — Same as HAL_CPU_HALT for ARM
;   ARM: wfi
;   x86 equivalent: hlt (or sti; hlt)
.macro HAL_CPU_WAIT_FOR_INTR
    wfi                          ; Wait For Interrupt (ARMv7-A / ARMv8-A)
.endm


; --- ARM-Specific Operations ---------------------------------------------

; HAL_CPU_SET_MODE mode — Switch to ARM exception mode (cps instruction)
;   mode: 0x10=USR, 0x11=FIQ, 0x12=IRQ, 0x13=SVC, 0x17=ABT, 0x1B=UND, 0x1F=SYS
;   Usage: HAL_CPU_SET_MODE #0x13   (switch to SVC mode)
.macro HAL_CPU_SET_MODE mode
    cps #\mode                  ; Change Processor State (switch mode)
.endm

; HAL_CPU_ENABLE_MMU — Enable MMU (set SCTLR.M bit)
;   ARM: mrc p15, 0, r0, c1, c0, 0   (read SCTLR)
;         orr r0, r0, #0x1             (set M bit)
;         mcr p15, 0, r0, c1, c0, 0   (write SCTLR)
.macro HAL_CPU_ENABLE_MMU
    mrc p15, 0, r0, c1, c0, 0   ; Read SCTLR
    orr r0, r0, #0x1             ; Set M bit (MMU enable)
    mcr p15, 0, r0, c1, c0, 0   ; Write SCTLR
    dsb                          ; Data Synchronization Barrier
    isb                          ; Instruction Synchronization Barrier
.endm

; HAL_CPU_DISABLE_MMU — Disable MMU (clear SCTLR.M bit)
.macro HAL_CPU_DISABLE_MMU
    mrc p15, 0, r0, c1, c0, 0   ; Read SCTLR
    bic r0, r0, #0x1             ; Clear M bit (MMU disable)
    mcr p15, 0, r0, c1, c0, 0   ; Write SCTLR
    dsb
    isb
.endm

; HAL_CPU_ENABLE_CACHE — Enable I-cache and D-cache (set SCTLR.I and C bits)
.macro HAL_CPU_ENABLE_CACHE
    mrc p15, 0, r0, c1, c0, 0   ; Read SCTLR
    orr r0, r0, #0x1000          ; Set I bit (Instruction cache enable)
    orr r0, r0, #0x4             ; Set C bit (Data cache enable)
    mcr p15, 0, r0, c1, c0, 0   ; Write SCTLR
    dsb
    isb
.endm

; HAL_CPU_DISABLE_CACHE — Disable I-cache and D-cache (clear SCTLR.I and C bits)
.macro HAL_CPU_DISABLE_CACHE
    mrc p15, 0, r0, c1, c0, 0   ; Read SCTLR
    bic r0, r0, #0x1000          ; Clear I bit (Instruction cache disable)
    bic r0, r0, #0x4             ; Clear C bit (Data cache disable)
    mcr p15, 0, r0, c1, c0, 0   ; Write SCTLR
    dsb
    isb
.endm


; --- Notes for ARM Port ---------------------------------------------
; 1. This file uses ARM assembly syntax (GAS), NOT NASM syntax!
; 2. For x86 port, use arch/x86/arch_cpu.inc (NASM syntax).
; 3. ARM does NOT have GDT/IDT — uses VBAR for exception vectors.
; 4. ARM does NOT have TSS — uses banked SP for each mode.
; 5. ARM MMU uses TTBR0/TTBR1, not CR3.
; 6. ARM has separate I-cache and D-cache (x86 has unified L2/L3).
; 7. ARM uses memory-mapped I/O (not port I/O like x86).
; 8. For Pi 4 / CM4: BCM2711 peripherals at 0xFE000000 (see BCM2711 ARM Peripherals manual).
; =============================================================================
