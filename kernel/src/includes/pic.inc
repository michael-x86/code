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
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; pic_remap — reprogram both 8259A PICs
;
; ICW1: edge-triggered, cascaded, ICW4 needed
; ICW2: master offset 0x20, slave offset 0x28
; ICW3: master has slave on IRQ2, slave is cascade identity 2
; ICW4: 8086 mode
;-- OCW1: unmask IRQ0 (timer) and IRQ1 (keyboard) on the master; mask
;-- everything on the slave (we don't use the slave's IRQs).
; -----------------------------------------------------------------------------
pic_remap:
    mov al, PIC_INIT
    out PIC_MASTER_CMD, al
    out PIC_SLAVE_CMD, al
    mov al, PIC_MASTER_OFFSET
    out PIC_MASTER_DATA, al
    out PIC_SLAVE_DATA, al               ; slave offset
    mov al, 0x04                         ; master: slave is at IRQ2
    out PIC_MASTER_DATA, al
    mov al, 0x02                         ; slave cascade identity
    out PIC_SLAVE_DATA, al
    mov al, 0x01                         ; 8086 mode
    out PIC_MASTER_DATA, al
    out PIC_SLAVE_DATA, al
    mov al, 0b11111100                   ; unmask IRQ0 + IRQ1, mask the rest
    out PIC_MASTER_DATA, al
    mov al, 0b11111111                   ; mask all slave IRQs
    out PIC_SLAVE_DATA, al
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
    out 0x43, al
    mov ax, PIT_DIVISOR
    out 0x40, al                         ; low byte
    mov al, ah
    out 0x40, al                         ; high byte
    ret
