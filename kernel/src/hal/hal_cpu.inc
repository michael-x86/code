; =============================================================================
; hal_cpu.inc — Architecture-agnostic CPU control interface
; =============================================================================
; USAGE: Kernel code calls these macros. Architecture-specific code in
;        arch/<arch>/arch_cpu.inc provides the implementation.
;
; PORTING: For ARM, create arch/arm/arch_cpu.inc implementing these
;          using ARM instructions (cpsid, cpsie, mrs, msr, wfi, etc.)
; =============================================================================


; --- Interrupt control ---------------------------------------------------

%macro HAL_DISABLE_INTS 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_DISABLE_INTS
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_DISABLE_INTS
    %endif
%endmacro

%macro HAL_ENABLE_INTS 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_ENABLE_INTS
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_ENABLE_INTS
    %endif
%endmacro

%macro HAL_SAVE_INT_STATE 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_SAVE_INT_STATE
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_SAVE_INT_STATE
    %endif
%endmacro

%macro HAL_RESTORE_INT_STATE 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_RESTORE_INT_STATE
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_RESTORE_INT_STATE
    %endif
%endmacro


; --- Descriptor table loads -----------------------------------------------

%macro HAL_LOAD_GDT 1
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_LOAD_GDT %1
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_LOAD_GDT %1
    %endif
%endmacro

%macro HAL_LOAD_IDT 1
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_LOAD_IDT %1
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_LOAD_IDT %1
    %endif
%endmacro

%macro HAL_LOAD_TSS 1
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_LOAD_TSS %1
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_LOAD_TSS %1
    %endif
%endmacro


; --- Control registers ---------------------------------------------------

%macro HAL_READ_CR 2
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_READ_CR %1, %2
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_READ_CR %1, %2
    %endif
%endmacro

%macro HAL_WRITE_CR 2
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_WRITE_CR %1, %2
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_WRITE_CR %1, %2
    %endif
%endmacro


; --- Cache/TLB control -------------------------------------------------

%macro HAL_INVLPG 1
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_INVLPG %1
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_INVLPG %1
    %endif
%endmacro

%macro HAL_FLUSH_CACHE 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_FLUSH_CACHE
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_FLUSH_CACHE
    %endif
%endmacro

%macro HAL_FLUSH_TLB 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_FLUSH_TLB
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_FLUSH_TLB
    %endif
%endmacro


; --- Halt/wait ---------------------------------------------------------

%macro HAL_HALT 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_HALT
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_HALT
    %endif
%endmacro

%macro HAL_WAIT_FOR_INTR 0
    %if HAL_ARCH == HAL_ARCH_X86
        HAL_CPU_WAIT_FOR_INTR
    %elif HAL_ARCH == HAL_ARCH_ARM
        HAL_CPU_WAIT_FOR_INTR
    %endif
%endmacro
