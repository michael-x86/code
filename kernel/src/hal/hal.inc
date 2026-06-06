; =============================================================================
; hal.inc — Main HAL (Hardware Abstraction Layer) header
; =============================================================================
; USAGE:
;   In kernel.asm or other includes, use:
;     %include "hal.inc"
;
;   This file selects the correct architecture implementation based on HAL_ARCH
;   and includes all necessary hal_*.inc files.
;
; ARCHITECTURE SELECTION:
;   Default: x86 (if HAL_ARCH not defined)
;   To select a different architecture, define HAL_ARCH before %including hal.inc:
;     %define HAL_ARCH 1   (1=x86, 2=arm, 3=riscv)
;     %include "hal.inc"
;
;   Or set from command line: nasm -D HAL_ARCH=2
;
; PORTING TO A NEW ARCHITECTURE:
;   1. Create arch/<arch>/ directory (e.g., arch/arm/)
;   2. Implement all HAL_* macro interfaces in arch/<arch>/arch_*.inc
;   3. Add %elif HAL_ARCH == <number> section below
;   4. Test on actual hardware or emulator
; =============================================================================


; --- Architecture numeric constants -----------------------------------------

%define HAL_ARCH_X86    1
%define HAL_ARCH_ARM    2
%define HAL_ARCH_RISCV  3


; --- Select default architecture (if not already defined) -----------------

%ifndef HAL_ARCH
    %define HAL_ARCH HAL_ARCH_X86       ; Default to x86
%endif


; --- Include architecture-specific implementations -------------------------

%if HAL_ARCH == HAL_ARCH_X86
    %include "arch/x86/arch_io.inc"
    ; TODO: Add more x86 implementation files as created:
    ; %include "arch/x86/arch_cpu.inc"
    ; %include "arch/x86/arch_intr.inc"
    ; %include "arch/x86/arch_mem.inc"
    ; %include "arch/x86/arch_timer.inc"
    ; %include "arch/x86/arch_debug.inc"

%elif HAL_ARCH == HAL_ARCH_ARM
    %include "arch/arm/arch_io.inc"
    ; %include "arch/arm/arch_cpu.inc"
    ; etc.

%elif HAL_ARCH == HAL_ARCH_RISCV
    %include "arch/riscv/arch_io.inc"
    ; etc.

%else
    %error "Unknown HAL_ARCH. Supported: 1 (x86), 2 (arm), 3 (riscv). Define HAL_ARCH correctly."
%endif


; --- HAL Interface Documentation -----------------------------------------
; The following macros are now defined (by the arch-specific files above).
; Kernel code should ONLY use these HAL_* macros, never arch-specific code.
;
; I/O (hal_io.inc interface):
;   HAL_PIC_WRITE_CMD master/slave
;   HAL_PIC_WRITE_DATA master/slave
;   HAL_PIC_READ_CMD master/slave
;   HAL_PIC_READ_DATA master/slave
;   HAL_PIT_WRITE_CMD
;   HAL_PIT_WRITE_DATA channel
;   HAL_PIT_READ_CMD
;   HAL_PIT_READ_DATA channel
;   HAL_UART_WRITE port, reg
;   HAL_UART_READ port, reg
;   HAL_KBD_WRITE_DATA
;   HAL_KBD_WRITE_CMD
;   HAL_KBD_READ_DATA
;   HAL_KBD_READ_STATUS
;   HAL_IO_WRITE port, reg
;   HAL_IO_READ port, reg
;   HAL_MMIO_WRITE addr, reg
;   HAL_MMIO_READ addr, reg
;
; CPU (hal_cpu.inc interface) - NOT YET IMPLEMENTED:
;   HAL_DISABLE_INTS
;   HAL_ENABLE_INTS
;   HAL_SAVE_INT_STATE
;   HAL_RESTORE_INT_STATE
;   HAL_LOAD_GDT addr
;   HAL_LOAD_IDT addr
;   HAL_LOAD_TSS selector
;   HAL_READ_CR num, reg
;   HAL_WRITE_CR num, reg
;   HAL_INVLPG addr
;   HAL_FLUSH_TLB
;   HAL_HALT
;   HAL_WAIT_FOR_INTR
;
; Interrupt Controller (hal_intr.inc interface) - NOT YET IMPLEMENTED:
;   HAL_INTR_INIT
;   HAL_INTR_MASK irq_num
;   HAL_INTR_UNMASK irq_num
;   HAL_INTR_EOI irq_num
;   etc.
;
; Memory Management (hal_mem.inc interface) - NOT YET IMPLEMENTED:
;   HAL_MMU_ENABLE
;   HAL_MMU_DISABLE
;   HAL_MMU_MAP virt, phys, flags
;   HAL_MMU_UNMAP virt
;   etc.
;
; Timer (hal_timer.inc interface) - NOT YET IMPLEMENTED:
;   HAL_TIMER_INIT frequency
;   HAL_TIMER_GET_TICKS
;   HAL_TIMER_SET_HANDLER label
;   etc.
;
; Debug Output (hal_debug.inc interface) - NOT YET IMPLEMENTED:
;   HAL_PUTCHAR char
;   HAL_PUTS string_label
;   HAL_SCREEN_CLEAR
;   etc.
; =============================================================================
