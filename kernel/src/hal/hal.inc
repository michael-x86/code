; =============================================================================
; hal.inc — Main HAL (Hardware Abstraction Layer) header
; =============================================================================
; USAGE:
;   In kernel.asm or other includes, use:
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
;   Include guard: HAL_INC (prevents multiple inclusion)
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

%ifndef HAL_INC
%define HAL_INC


; --- Architecture numeric constants -----------------------------------------

%define HAL_ARCH_X86    1
%define HAL_ARCH_ARM    2
%define HAL_ARCH_RISCV  3


; --- Select default architecture (if not already defined) -----------------

%ifndef HAL_ARCH
    %define HAL_ARCH HAL_ARCH_X86       ; Default to x86
%endif


; --- Include HAL interface files -----------------------------------------

%include "hal_cpu.inc"           ; CPU control interface (cli/sti, lgdt, etc.)


; --- Include architecture-specific implementations -------------------------

%if HAL_ARCH == HAL_ARCH_X86
    %include "arch/x86/arch_io.inc"
    %include "arch/x86/arch_cpu.inc"    ; x86 CPU control (cli, sti, lgdt, etc.)
    ; TODO: Add more x86 implementation files as created:
    ; %include "arch/x86/arch_intr.inc"
    ; %include "arch/x86/arch_mem.inc"
    ; %include "arch/x86/arch_timer.inc"
    ; %include "arch/x86/arch_debug.inc"

%elif HAL_ARCH == HAL_ARCH_ARM
    %include "arch/arm/arch_io.inc"
    %include "arch/arm/arch_cpu.inc"    ; ARM CPU control (cpsid, cpsie, mrs, etc.)
    ; TODO: Add more ARM implementation files:
    ; %include "arch/arm/arch_intr.inc"
    ; %include "arch/arm/arch_mem.inc"
    ; %include "arch/arm/arch_timer.inc"
    ; %include "arch/arm/arch_debug.inc"

%elif HAL_ARCH == HAL_ARCH_RISCV
    %include "arch/riscv/arch_io.inc"
    ; etc.

%else
    %error "Unknown HAL_ARCH. Supported: 1 (x86), 2 (arm), 3 (riscv). Define HAL_ARCH correctly."
%endif

%endif                              ; End of HAL_INC include guard
