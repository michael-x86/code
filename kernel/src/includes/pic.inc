; =============================================================================
; pic.inc — 8259A PIC remap + PIT channel 0 frequency setup
; =============================================================================
;
; The BIOS leaves the master PIC's IRQ vectors at 0x08..0x0F, which
;-- conflict with CPU exceptions 8..15 (double fault, segment not
;-- present, etc.). We remap so the master PIC delivers INT 0x20..0x27
;-- and the slave PIC delivers INT 0x28..0x2F. The CPU exceptions keep
;-- vectors 0..31.
;
; The PIT (Programmable Interval Timer) is a separate chip, but it
;-- sits on the PIC's IRQ0 line. set_freq configures channel 0 to
;-- PIT_FREQUENCY Hz, which is what drives our preemptive scheduler.
;
; NOTE: This file now uses HAL (Hardware Abstraction Layer) macros for
;-- I/O operations. See arch/x86/arch_io.inc for x86 implementation.
; =============================================================================

%include "hal/hal.inc"

section .text


; -----------------------------------------------------------------------------
; pic_remap — reprogram both 8259A PICs
;
; ICW1: edge-triggered, cascaded, ICW4 needed
; ICW2: master offset 0x20, slave offset 0x28
; ICW3: master has slave on IRQ2, slave is cascade identity 2
; ICW4: 8086 mode
;-- OCW1: unmask IRQ0 (timer), IRQ1 (keyboard), and IRQ2 (slave cascade) on
;-- the master; unmask IRQ12 (PS/2 mouse) on the slave, mask the rest.
; -----------------------------------------------------------------------------
pic_remap:
    mov al, PIC_INIT
    HAL_PIC_WRITE_CMD master
    HAL_PIC_WRITE_CMD slave
    mov al, PIC_MASTER_OFFSET
    HAL_PIC_WRITE_DATA master
    HAL_PIC_WRITE_DATA slave               ; slave offset
    mov al, 0x04                         ; master: slave is at IRQ2
    HAL_PIC_WRITE_DATA master
    mov al, 0x02                         ; slave cascade identity
    HAL_PIC_WRITE_DATA slave
    mov al, 0x01                         ; 8086 mode
    HAL_PIC_WRITE_DATA master
    HAL_PIC_WRITE_DATA slave
    mov al, 0b11111000                   ; unmask IRQ0/1/2 (2 = slave cascade)
    HAL_PIC_WRITE_DATA master
    mov al, 0b11101111                   ; unmask IRQ12 (mouse) on the slave
    HAL_PIC_WRITE_DATA slave
    ret


; -----------------------------------------------------------------------------
; set_freq — program PIT channel 0 to PIT_FREQUENCY Hz
;
; PIT base clock is 1,193,182 Hz. With divisor D, output frequency is
;-- 1193182 / D. We pick D = 11932 for exactly 100 Hz, which is the
;-- tick rate the scheduler and the shell's live-tick display assume.
;
; Mode 0x36 = channel 0, lobyte/hibyte access, square-wave generator.
; The low byte of the divisor goes first, then the high byte.
; -----------------------------------------------------------------------------
set_freq:
    mov al, 0x36
    HAL_PIT_WRITE_CMD
    mov ax, PIT_DIVISOR
    HAL_PIT_WRITE_DATA 0                         ; low byte
    mov al, ah
    HAL_PIT_WRITE_DATA 0                         ; high byte
    ret
