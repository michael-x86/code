[bits 32]
[org 0x00000000]

_start:
    mov eax,0              
    ;int 0x80

    ; 1. Trigger QEMU isa-debug-exit device on port 0x501 (8-bit write)
    mov dx, 0x501
    mov al, 0x10        ; (0x10 << 1) | 1 = 33 -> Clean exit with status code 33
    out dx, al

    ; 2. ACPI S5 Poweroff fallback for QEMU / Bochs (Port 0x604)
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax

    ; 3. System shutdown fallback for older QEMU ACPI (Port 0xB004)
    mov dx, 0xB004
    mov ax, 0x2000
    out dx, ax

    cli
    hlt
