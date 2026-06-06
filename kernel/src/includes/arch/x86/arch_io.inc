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

%define PIT_CMD_PORT   0x43
%define PIT_CH0_PORT   0x40
%define PIT_CH1_PORT   0x41
%define PIT_CH2_PORT   0x42

; HAL_PIT_WRITE_CMD  (writes al to PIT command port 0x43)
%macro HAL_PIT_WRITE_CMD 0
    out PIT_CMD_PORT, al
%endmacro

; HAL_PIT_WRITE_DATA channel  (writes al/ax to PIT channel port)
;   channel = 0, 1, or 2
%macro HAL_PIT_WRITE_DATA 1
    %if %1 == 0
        out PIT_CH0_PORT, al
    %elif %1 == 1
        out PIT_CH1_PORT, al
    %elif %1 == 2
        out PIT_CH2_PORT, al
    %else
        %error "HAL_PIT_WRITE_DATA: channel must be 0, 1, or 2"
    %endif
%endmacro

; HAL_PIT_READ_CMD  (reads PIT command port into al)
%macro HAL_PIT_READ_CMD 0
    in al, PIT_CMD_PORT
%endmacro

; HAL_PIT_READ_DATA channel  (reads PIT channel port into al)
%macro HAL_PIT_READ_DATA 1
    %if %1 == 0
        in al, PIT_CH0_PORT
    %elif %1 == 1
        in al, PIT_CH1_PORT
    %elif %1 == 2
        in al, PIT_CH2_PORT
    %endif
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
