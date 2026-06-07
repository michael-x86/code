/*
 * =============================================================================
 * arch/arm/arch_io.inc — ARM I/O implementation (memory-mapped)
 * =============================================================================
 * 
 * ARM uses memory-mapped I/O (not x86 port I/O).
 * Peripherals are accessed via LDR/STR to specific addresses.
 *
 * BCM2711 (Pi 4 / CM4) peripheral base: 0xFE000000
 *
 * Assembler: GAS (GNU Assembler) ARM syntax
 * Registers: r0-r15 (not eax, ebx, etc.)
 * Syntax:    ldr r0, =0xFE001000  (load address)
 *            ldr r1, [r0]          (load from address)
 *            str r1, [r0]          (store to address)
 *
 * NOTE: This file uses ARM GAS syntax. It will NOT assemble with NASM.
 *       When building for ARM, use arm-none-eabi-gcc/as.
 * =============================================================================
 */

/* =============================================================================
 * SYNTAX GUIDE: ARM GAS Assembly (for x86 developers)
 * =============================================================================
 *
 * This section explains ARM-specific syntax that differs from x86 NASM.
 * See: https://sourceware.org/binutils/docs-2.40/as/
 *
 * -----------------------------------------------------------------------------
 * 1. THE '=' PREFIX (Load ADDRESS, not VALUE)
 * -----------------------------------------------------------------------------
 *
 * In ARM GAS, '=' tells the assembler to LOAD THE CONSTANT into the register,
 * not to load FROM that memory address.
 *
 * WITHOUT '=' (WRONG for constants):
 *   ldr r0, GIC_CPU_BASE    ; Tries to load FROM memory at GIC_CPU_BASE
 *                             ; (Executes: r0 = *(uint32_t*)0xFE00C000)
 *
 * WITH '=' (CORRECT for constants):
 *   ldr r0, =GIC_CPU_BASE   ; Loads the ADDRESS into r0
 *                             ; (Executes: r0 = 0xFE00C000)
 *
 * WHY?
 *   ARM instructions are fixed 32-bit. You can't encode a 32-bit constant
 *   directly in 'mov' (only 8-bit + rotate). The assembler works around this:
 *     1. Puts the constant in a "literal pool" (data section nearby)
 *     2. Generates: ldr r0, [pc, #offset]  (PC-relative load)
 *
 * EXAMPLE EXPANSION:
 *   ldr r0, =0xFE00C010
 *     ↓ Assembler converts to:
 *   ldr r0, .Lpool1
 *   b   .skip_pool
 *   .Lpool1: .word 0xFE00C010
 *   .skip_pool:
 *
 * -----------------------------------------------------------------------------
 * 2. MACRO PARAMETER SUBSTITUTION ('\' BACKSLASH)
 * -----------------------------------------------------------------------------
 *
 * In ARM GAS macros, parameters are referenced with '\' prefix (named parameters).
 *
 * DEFINITION:
 *   .macro HAL_PIC_EOI irq          ; Parameter name: 'irq'
 *       ldr r0, =GIC_CPU_BASE
 *       mov r1, \irq                 ; Use '\' to substitute 'irq'
 *       str r1, [r0]
 *   .endm
 *
 * USAGE:
 *   HAL_PIC_EOI 5                   ; Pass 5 as 'irq'
 *
 * EXPANSION:
 *   ldr r0, .Lpool1                ; r0 = 0xFE00C000
 *   mov r1, 5                       ; r1 = 5 (IRQ number)
 *   str r1, [r0]                    ; Write to GICC_EOIR
 *
 * NAMED PARAMETERS (ARM GAS):
 *   .macro MY_MACRO param1, param2
 *       mov r0, \param1
 *       mov r1, \param2
 *   .endm
 *
 * POSITIONAL PARAMETERS (NASM, for comparison):
 *   %macro MY_MACRO 2               ; 2 parameters
 *       mov eax, %1                 ; First parameter
 *       mov ebx, %2                 ; Second parameter
 *   %endmacro
 *
 * -----------------------------------------------------------------------------
 * 3. NASM vs ARM GAS QUICK REFERENCE
 * -----------------------------------------------------------------------------
 *
 * | Feature               | NASM (x86)              | ARM GAS                 |
 * |----------------------|--------------------------|--------------------------|
 * | Registers            | eax, ebx, ecx, edx     | r0, r1, r2, r3          |
 * | Load constant        | mov eax, 0x3F8          | ldr r0, =0x3F8          |
 * | Load from memory     | mov eax, [0x3F8]        | ldr r0, =0x3F8 + load   |
 * | Store to memory      | mov [0x3F8], eax        | str r0, [r1]            |
 * | Macro definition     | %macro NAME 1            | .macro NAME param        |
 * | Macro parameter      | %1, %2, %3...           | \param (named)          |
 * | Conditional jump     | jz, jnz, jc, jnc        | b.eq, b.ne, b.cs, b.cc |
 * | Function call        | call my_func             | bl my_func              |
 * | Return               | ret                      | bx lr                   |
 * | Push                 | push eax                 | stmfd sp!, {r0}        |
 * | Pop                  | pop eax                  | ldmfd sp!, {r0}        |
 *
 * -----------------------------------------------------------------------------
 * 4. COMMON ARM INSTRUCTION PATTERNS
 * -----------------------------------------------------------------------------
 *
 * LOAD 32-BIT CONSTANT:
 *   ldr r0, =0xFE00C000        ; r0 = 0xFE00C000
 *
 * LOAD FROM MEMORY:
 *   ldr r0, =GIC_CPU_BASE      ; r0 = address of GICC_EOIR
 *   ldr r1, [r0]                ; r1 = *(uint32_t*)r0
 *
 * STORE TO MEMORY:
 *   ldr r0, =GIC_CPU_BASE
 *   mov r1, #5
 *   str r1, [r0]                ; *(uint32_t*)r0 = r1
 *
 * MACRO WITH PARAMETER:
 *   .macro HAL_WRITE reg, val
 *       ldr r0, =\reg
 *       mov r1, \val
 *       str r1, [r0]
 *   .endm
 *
 *   HAL_WRITE UART0_DR, 'A'     ; Expands to:
 *                              ;   ldr r0, =UART0_DR
 *                              ;   mov r1, #'A'
 *                              ;   str r1, [r0]
 *
 * -----------------------------------------------------------------------------
 * 5. ARM-SPECIFIC INSTRUCTIONS (no x86 equivalent)
 * -----------------------------------------------------------------------------
 *
 * COPROCESSOR ACCESS (timer, MMU, etc.):
 *   mcr p15, 0, r0, c14, c2, 1   ; Write r0 to CNTP_CTL
 *   mrc p15, 0, r0, c14, c2, 1   ; Read CNTP_CTL into r0
 *
 * LOAD/STORE 64-BIT (for timer counter):
 *   mcrr p15, 0, r0, r1, c14, c0  ; Write r0:r1 to CNTPCT (64-bit)
 *   mrrc p15, 0, r0, r1, c14, c0  ; Read CNTPCT into r0:r1
 *
 * CACHE/TLB OPERATIONS:
 *   mcr p15, 0, r0, c8, c7, 1    ; Invalidate TLB entry (TLBIMVA)
 *   mcr p15, 0, r0, c7, c0, 4    ; Wait For Interrupt (WFI)
 *
 * INTERRUPT CONTROL:
 *   cpsid i                        ; Disable interrupts (set PRIMASK)
 *   cpsie i                        ; Enable interrupts (clear PRIMASK)
 *
 * =============================================================================
 * END SYNTAX GUIDE
 * =============================================================================
 */

/* --- Peripheral addresses (BCM2711) ------------------------------------- */

#define BCM_PERIPHERAL_BASE    0xFE000000

#define UART0_BASE              0xFE001000      /* PL011 UART */
#define UART1_BASE              0xFE002000      /* mini UART */
#define GIC_DIST_BASE           0xFE00B000      /* GIC-400 Distributor */
#define GIC_CPU_BASE            0xFE00C000      /* GIC-400 CPU Interface */
#define GPIO_BASE               0xFE200000      /* GPIO controller */

/* --- UART0 (PL011) register offsets --------------------------------------- */

#define UART0_DR               0x00            /* Data Register */
#define UART0_RSR              0x04            /* Receive Status Register */
#define UART0_FR               0x18            /* Flag Register */
#define UART0_IBRD             0x24            /* Integer Baud Rate Divisor */
#define UART0_FBRD             0x28            /* Fractional Baud Rate Divisor */
#define UART0_LCRH             0x2C            /* Line Control Register */
#define UART0_CR               0x30            /* Control Register */
#define UART0_IFLS             0x34            /* Interrupt FIFO Level Select */
#define UART0_IMSC             0x38            /* Interrupt Mask Set/Clear */
#define UART0_RIS              0x3C            /* Raw Interrupt Status */
#define UART0_MIS              0x40            /* Masked Interrupt Status */
#define UART0_ICR              0x44            /* Interrupt Clear Register */

/* --- GIC-400 register offsets -------------------------------------------- */

#define GICD_CTLR              0x000           /* Distributor Control */
#define GICD_ISENABLER         0x100           /* Interrupt Set-Enable */
#define GICD_ICENABLER         0x180           /* Interrupt Clear-Enable */
#define GICD_ISPENDR           0x200           /* Interrupt Set-Pending */
#define GICD_ICPENDR           0x280           /* Interrupt Clear-Pending */
#define GICD_IPRIORITYR        0x400           /* Interrupt Priority */
#define GICD_ITARGETSR         0x800           /* Interrupt Targets */
#define GICD_ICFGR             0xC00           /* Interrupt Configuration */

#define GICC_CTLR              0x000           /* CPU Interface Control */
#define GICC_PMR               0x004           /* Priority Mask */
#define GICC_BPR               0x008           /* Binary Point */
#define GICC_IAR               0x00C           /* Interrupt Acknowledge */
#define GICC_EOIR              0x010           /* End of Interrupt */

/* --- HAL_READ_PORT <addr>, <reg> - Read from memory-mapped I/O -----------
 * Args: addr (immediate), reg (ARM register)
 * Clobbers: r0 (tmp)
 */

.macro HAL_READ_PORT addr, reg
    ldr r0, =\addr
    ldr \reg, [r0]
.endm

/* --- HAL_WRITE_PORT <addr>, <reg> - Write to memory-mapped I/O ----------
 * Args: addr (immediate), reg (ARM register)
 * Clobbers: r0 (tmp)
 */

.macro HAL_WRITE_PORT addr, reg
    ldr r0, =\addr
    str \reg, [r0]
.endm

/* --- PIC (GIC-400) macros ------------------------------------------------ */

/* HAL_PIC_WRITE_CMD - Initialize GIC distributor
 * Clobbers: r0, r1
 */

.macro HAL_PIC_WRITE_CMD
    ldr r0, =GIC_DIST_BASE
    mov r1, #1
    str r1, [r0]                          @ GICD_CTLR = 1 (enable)
.endm

/* HAL_PIC_WRITE_DATA <irq> - Enable specific IRQ
 * Args: irq (immediate, 0-159 for GIC-400)
 * Clobbers: r0, r1, r2
 */

.macro HAL_PIC_WRITE_DATA irq
    ldr r0, =GIC_DIST_BASE + GICD_ISENABLER
    mov r1, \irq
    lsr r2, r1, #5                        @ r2 = irq / 32
    and r1, r1, #31                        @ r1 = irq % 32
    mov r3, #1
    lsl r3, r3, r1                        @ r3 = (1 << (irq % 32))
    str r3, [r0, r2, lsl #2]             @ GICD_ISENABLER[irq/32] = bit
.endm

/* HAL_PIC_READ_CMD - Read GIC distributor control
 * Returns: al (byte read, x86 compatibility) or r0 (ARM native)
 * Clobbers: r0
 */

.macro HAL_PIC_READ_CMD
    ldr r0, =GIC_DIST_BASE
    ldr r0, [r0]                          @ r0 = GICD_CTLR
    /* For x86 compatibility: copy to al (lower 8 bits) */
    and r0, r0, #0xFF
.endm

/* HAL_PIC_READ_DATA - Read GIC pending interrupts
 * Returns: al (byte read) or r0 (ARM native)
 * Clobbers: r0
 */

.macro HAL_PIC_READ_DATA
    ldr r0, =GIC_DIST_BASE + GICD_ISPENDR
    ldr r0, [r0]                          @ r0 = GICD_ISPENDR[0]
    and r0, r0, #0xFF                     @ x86 compat: return byte
.endm

/* HAL_PIC_EOI <irq> - Send End-of-Interrupt to GIC CPU interface
 * Args: irq (immediate)
 * Clobbers: r0
 */

.macro HAL_PIC_EOI irq
    ldr r0, =GIC_CPU_BASE + GICC_EOIR
    mov r1, \irq
    str r1, [r0]
.endm

/* --- ARM Generic Timer (replaces x86 PIT) -----------------------------------
 * ARM has no PIT chip; it has a built-in Generic Timer accessed via
 * coprocessor registers.  The HAL abstraction hides this difference.
 *
 * Timer registers:
 *   CNTPCT    (r/o)  64-bit physical count, increments at CPU_TIMER_HZ
 *   CNTP_CVAL (r/w)  64-bit compare value; IRQ fires when CNTPCT >= CNTP_CVAL
 *   CNTP_CTL  (r/w)  Control: bit 0 = ENABLE, bit 1 = IMASK
 */

/* CPU_TIMER_HZ — frequency of the ARM generic timer on this platform.
 * BCM2711 (Pi 4 / CM4) runs the system timer at 100 MHz typically.
 * Override this in board-specific headers if different.
 */
#ifndef CPU_TIMER_HZ
#define CPU_TIMER_HZ  100000000   /* 100 MHz */
#endif

/* PIT_BASE_CLOCK — the reference frequency the divisor is calibrated against.
 * The HAL_TIMER_PROGRAM_CH0 macro multiplies back so the ARM Generic Timer
 * produces the same interrupt rate that the PIT would on x86.
 */
#define PIT_BASE_CLOCK  1193182   /* 1.193182 MHz PIT reference */

/* ---------------------------------------------------------------------------
 * HAL_TIMER_PROGRAM_CH0  divisor
 *   Program the system timer to generate periodic interrupts at:
 *       freq = PIT_BASE_CLOCK / divisor    (≈ 100 Hz with divisor = 11932)
 *
 *   ARM implementation: writes CNTP_CVAL = CNTPCT + tick_offset, then
 *   enables the timer.  tick_offset = CPU_TIMER_HZ * divisor / PIT_BASE_CLOCK.
 *
 *   Clobbers: r0-r4
 *
 *   NOTE: The runtime division is expensive.  For production, consider
 *   pre-computing tick_offset in the build system or using an arch-specific
 *   timer.h header that inlines the right constant.
 * ------------------------------------------------------------------------- */

.macro HAL_TIMER_PROGRAM_CH0 divisor
    push    {r4, r5, r6, r7, lr}

    /* Read current counter: r4:r5 = CNTPCT (64 bits) */
    mrrc    p15, 0, r4, r5, c14, c0

    /* tick_offset = CPU_TIMER_HZ * \divisor / PIT_BASE_CLOCK
     * We load the three constants and call a helper routine.
     * This is a work-in-progress; a future arch_timer.inc should
     * provide a pre-computed constant instead.                    @FIXME
     */
    ldr     r0, =CPU_TIMER_HZ
    ldr     r1, =\divisor
    ldr     r2, =PIT_BASE_CLOCK
    /* TODO: __aeabi_uidiv / __aeabi_uldiv style helper          @FIXME
     * For now, fall through — the following is a skeleton.
     * Replace with: bl  _hal_timer_compute_offset                 @FIXME
     */

    /* Add tick_offset (r0) to CNTPCT (r4:r5) → r0:r1 = CNTP_CVAL */
    adds    r0, r4, r0
    adc     r1, r5, #0

    /* Write CNTP_CVAL */
    mcrr    p15, 0, r0, r1, c14, c2

    /* Enable timer: CNTP_CTL = 1 (ENABLE=1, IMASK=0) */
    mov     r0, #1
    mcr     p15, 0, r0, c14, c2, 1

    pop     {r4, r5, r6, r7, pc}
.endm

/* --- UART (PL011) macros -------------------------------------------------- */

/* HAL_UART_WRITE <port> - Write byte to UART data register
 * Args: port (ignored, only one UART for now)
 * Input: al (byte to write, x86 compatibility) or r0 (ARM native)
 * Clobbers: r0, r1, r2
 */

.macro HAL_UART_WRITE port
    /* Wait for UART to be ready (UART0_FR bit 5 = TXFF) */
1:
    ldr r0, =UART0_BASE + UART0_FR
    ldr r1, [r0]
    tst r1, #(1 << 5)                     @ TXFF bit
    bne 1b                                 @ Loop if TX FIFO full
    
    /* Write byte to data register */
    ldr r0, =UART0_BASE + UART0_DR
    and r1, r0, #0xFF                     @ x86 compat: byte from al
    str r1, [r0]
.endm

/* HAL_UART_READ <port> - Read byte from UART data register
 * Args: port (ignored)
 * Returns: al (byte read) or r0 (ARM native)
 * Clobbers: r0, r1
 */

.macro HAL_UART_READ port
    /* Wait for data (UART0_FR bit 4 = RXFE) */
1:
    ldr r0, =UART0_BASE + UART0_FR
    ldr r1, [r0]
    tst r1, #(1 << 4)                     @ RXFE bit
    bne 1b                                 @ Loop if RX FIFO empty
    
    /* Read byte from data register */
    ldr r0, =UART0_BASE + UART0_DR
    ldr r1, [r0]
    and r0, r1, #0xFF                     @ x86 compat: return byte in r0
.endm

/* HAL_UART_INIT - Initialize PL011 UART
 * Clobbers: r0, r1
 */

.macro HAL_UART_INIT
    /* Disable UART: UART0_CR = 0 */
    ldr r0, =UART0_BASE + UART0_CR
    mov r1, #0
    str r1, [r0]
    
    /* Set baud rate (115200 with 48MHz clock: IBRD=26, FBRD=3) */
    ldr r0, =UART0_BASE + UART0_IBRD
    mov r1, #26
    str r1, [r0]
    
    ldr r0, =UART0_BASE + UART0_FBRD
    mov r1, #3
    str r1, [r0]
    
    /* Set line control: 8N1, FIFO enabled */
    ldr r0, =UART0_BASE + UART0_LCRH
    mov r1, #((1 << 4) | (3 << 5))       @ FEN=1, WLEN=3 (8 bits)
    str r1, [r0]
    
    /* Enable UART: UART0_CR = RXE|TXE|UARTEN */
    ldr r0, =UART0_BASE + UART0_CR
    mov r1, #((1 << 0) | (1 << 8) | (1 << 9))  @ UARTEN, TXE, RXE
    str r1, [r0]
.endm

/* --- Keyboard (GPIO) macros (PLACEHOLDER) --------------------------------
 * NOTE: Pi 4 keyboard is typically USB (via USB controller) or GPIO matrix.
 *       This is a placeholder for GPIO-based keyboard matrix.
 */

/* HAL_KEYBOARD_WRITE_CMD - Write to keyboard controller (GPIO)
 * Clobbers: r0, r1
 */

.macro HAL_KEYBOARD_WRITE_CMD
    /* Set GPIO pins as outputs (functional select) */
    ldr r0, =GPIO_BASE
    mov r1, #0b001                         @ ALT0 function (example)
    str r1, [r0]                           @ GPIO_FSEL0
.endm

/* HAL_KEYBOARD_READ_DATA - Read from keyboard controller (GPIO)
 * Returns: r0 (GPIO pin levels)
 * Clobbers: r0
 */

.macro HAL_KEYBOARD_READ_DATA
    ldr r0, =GPIO_BASE + 0x34              @ GPIO_LEV0
    ldr r0, [r0]                           @ r0 = GPIO pin levels
.endm

/* --- Generic I/O helpers -------------------------------------------------- */

/* HAL_IN <port>, <reg> - Read from I/O port (DEPRECATED: use HAL_READ_PORT)
 * NOTE: ARM doesn't have I/O ports. This is for x86 source compatibility.
 */

.macro HAL_IN port, reg
    HAL_READ_PORT \port, \reg
.endm

/* HAL_OUT <port>, <reg> - Write to I/O port (DEPRECATED: use HAL_WRITE_PORT)
 * NOTE: ARM doesn't have I/O ports. This is for x86 source compatibility.
 */

.macro HAL_OUT port, reg
    HAL_WRITE_PORT \port, \reg
.endm

/* --- BCM2711 specific helpers -------------------------------------------- */

/* HAL_BCM_READ <offset>, <reg> - Read from BCM peripheral at offset
 * Args: offset (immediate), reg (ARM register)
 * Clobbers: r0 (tmp)
 */

.macro HAL_BCM_READ offset, reg
    ldr r0, =(BCM_PERIPHERAL_BASE + \offset)
    ldr \reg, [r0]
.endm

/* HAL_BCM_WRITE <offset>, <reg> - Write to BCM peripheral at offset
 * Args: offset (immediate), reg (ARM register)
 * Clobbers: r0 (tmp)
 */

.macro HAL_BCM_WRITE offset, reg
    ldr r0, =(BCM_PERIPHERAL_BASE + \offset)
    str \reg, [r0]
.endm

/* --- Notes for porting x86 code to ARM -----------------------------------
 * 1. x86 registers (eax, ebx, ecx, edx) → ARM registers (r0, r1, r2, r3)
 * 2. x86 PUSH/POP → ARM stmfd/ldmfd (or just use registers)
 * 3. x86 conditional jumps (jz, jnz) → ARM b.eq, b.ne
 * 4. x86 IN/OUT → ARM ldr/str (memory-mapped I/O)
 * 5. x86 CLI/STI → ARM cpsid i / cpsie i (PRIMASK)
 * 6. x86 HLT → ARM wfi (Wait For Interrupt)
 * 7. x86 port I/O addresses (0x3F8) → ARM memory addresses (0xFE001000)
 * 8. x86 interrupt controllers (8259A PIC) → ARM GIC-400
 * 9. x86 PIT timer → ARM Generic Timer (coprocessor registers)
 * 10. x86 VGA text mode (0xB8000) → ARM framebuffer (HDMI)
 */
