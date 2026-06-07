; =============================================================================
; arch/x86/arch_io.inc — x86 I/O implementation
; =============================================================================
; This file implements the HAL I/O interface using x86 'in'/'out' instructions.
; For memory-mapped architectures (ARM), arch/arm/arch_io.inc would use
; load/store instructions instead.
;
; USAGE: This file is %included by hal.inc after the architecture is selected.
;        Kernel code calls HAL_* macros defined here.
; =============================================================================

; --- PIC (8259A Programmable Interrupt Controller) ----------------------------

%define PIC_MASTER_CMD   0x20
%define PIC_MASTER_DATA  0x21
%define PIC_SLAVE_CMD    0xA0
%define PIC_SLAVE_DATA   0xA1

; HAL_PIC_WRITE_CMD master/slave  (writes al to PIC command port)
%macro HAL_PIC_WRITE_CMD 1
    %ifidn %1, master
        out PIC_MASTER_CMD, al
    %elifidn %1, slave
        out PIC_SLAVE_CMD, al
    %else
        %error "HAL_PIC_WRITE_CMD: argument must be 'master' or 'slave'"
    %endif
%endmacro

; HAL_PIC_WRITE_DATA master/slave  (writes al to PIC data port)
%macro HAL_PIC_WRITE_DATA 1
    %ifidn %1, master
        out PIC_MASTER_DATA, al
    %elifidn %1, slave
        out PIC_SLAVE_DATA, al
    %else
        %error "HAL_PIC_WRITE_DATA: argument must be 'master' or 'slave'"
    %endif
%endmacro

; HAL_PIC_READ_CMD master/slave  (reads PIC command port into al)
%macro HAL_PIC_READ_CMD 1
    %ifidn %1, master
        in al, PIC_MASTER_CMD
    %elifidn %1, slave
        in al, PIC_SLAVE_CMD
    %else
        %error "HAL_PIC_READ_CMD: argument must be 'master' or 'slave'"
    %endif
%endmacro

; HAL_PIC_READ_DATA master/slave  (reads PIC data port into al)
%macro HAL_PIC_READ_DATA 1
    %ifidn %1, master
        in al, PIC_MASTER_DATA
    %elifidn %1, slave
        in al, PIC_SLAVE_DATA
    %else
        %error "HAL_PIC_READ_DATA: argument must be 'master' or 'slave'"
    %endif
%endmacro


; --- PIT (Programmable Interval Timer) ----------------------------------------
;
; The PIT uses x86 port I/O.  To keep the HAL portable, callers MUST NOT
; directly manipulate PIT registers or use x86-specific registers (al, ax, etc.).
; Use HAL_TIMER_PROGRAM_CH0 instead.
;
; =============================================================================
; HAL_TIMER_PROGRAM_CH0  divisor
;   Program PIT channel 0 in mode 3 (square-wave generator) for the given
;   16-bit divisor.  The output frequency is 1,193,182 Hz / divisor.
;
;   Modern x86 systems also provide the HPET, but we target 586-class
;   compatibility, so we use the legacy PIT/8254.
;
;   Clobbers: ax (16-bit), agnostic of architecture-specific register names.
; =============================================================================

%macro HAL_TIMER_PROGRAM_CH0 1
    mov al, 0x36               ; channel 0, lobyte/hibyte, mode 3, binary
    out 0x43, al
    mov ax, %1                 ; divisor
    out 0x40, al               ; low byte
    mov al, ah
    out 0x40, al               ; high byte
%endmacro


; --- UART (Serial Port) ------------------------------------------------------

%define UART_PORT_1     0x3F8
%define UART_PORT_2     0x2F8
%define UART_PORT_3     0x3E8
%define UART_PORT_4     0x2E8

; HAL_UART_WRITE port, reg  (writes reg to UART port)
;   port = 1, 2, 3, 4  (maps to 0x3F8, 0x2F8, 0x3E8, 0x2E8)
;   reg  = register containing data (al, ax, eax)
%macro HAL_UART_WRITE 2
    %if %1 == 1
        out UART_PORT_1, %2
    %elif %1 == 2
        out UART_PORT_2, %2
    %elif %1 == 3
        out UART_PORT_3, %2
    %elif %1 == 4
        out UART_PORT_4, %2
    %else
        %error "HAL_UART_WRITE: port must be 1, 2, 3, or 4"
    %endif
%endmacro

; HAL_UART_READ port, reg  (reads UART port into reg)
%macro HAL_UART_READ 2
    %if %1 == 1
        in %2, UART_PORT_1
    %elif %1 == 2
        in %2, UART_PORT_2
    %elif %1 == 3
        in %2, UART_PORT_3
    %elif %1 == 4
        in %2, UART_PORT_4
    %endif
%endmacro


; --- Keyboard Controller (i8042) ---------------------------------------------

%define KBD_DATA_PORT   0x60
%define KBD_CMD_PORT    0x64
%define KBD_STATUS_PORT 0x64

; HAL_KBD_WRITE_DATA  (writes al to keyboard data port)
%macro HAL_KBD_WRITE_DATA 0
    out KBD_DATA_PORT, al
%endmacro

; HAL_KBD_WRITE_CMD  (writes al to keyboard command port)
%macro HAL_KBD_WRITE_CMD 0
    out KBD_CMD_PORT, al
%endmacro

; HAL_KBD_READ_DATA  (reads keyboard data port into al)
%macro HAL_KBD_READ_DATA 0
    in al, KBD_DATA_PORT
%endmacro

; HAL_KBD_READ_STATUS  (reads keyboard status port into al)
%macro HAL_KBD_READ_STATUS 0
    in al, KBD_STATUS_PORT
%endmacro


; --- Generic I/O (use only if device-specific macros don't exist) -------------

; HAL_IO_WRITE port, reg  (writes reg to I/O port)
;   port = immediate (0x3F8) or register (dx)
;   reg  = al, ax, or eax
%macro HAL_IO_WRITE 2
    %ifnidn %1, dx
        mov dx, %1
    %endif
    %ifidn %2, al
        out dx, al
    %elifidn %2, ax
        out dx, ax
    %elifidn %2, eax
        out dx, eax
    %else
        %error "HAL_IO_WRITE: reg must be al, ax, or eax"
    %endif
%endmacro

; HAL_IO_READ port, reg  (reads I/O port into reg)
%macro HAL_IO_READ 2
    %ifnidn %1, dx
        mov dx, %1
    %endif
    %ifidn %2, al
        in al, dx
    %elifidn %2, ax
        in ax, dx
    %elifidn %2, eax
        in eax, dx
    %else
        %error "HAL_IO_READ: reg must be al, ax, or eax"
    %endif
%endmacro


; --- Memory-Mapped I/O (for architectures without port I/O) -------------------

; HAL_MMIO_WRITE addr, reg  (writes reg to memory-mapped I/O address)
;   addr = immediate address or register
;   reg  = al, ax, eax, or 32-bit register
%macro HAL_MMIO_WRITE 2
    mov [%1], %2
%endmacro

; HAL_MMIO_READ addr, reg  (reads memory-mapped I/O address into reg)
%macro HAL_MMIO_READ 2
    mov %2, [%1]
%endmacro
