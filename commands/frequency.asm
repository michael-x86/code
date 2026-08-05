; frequency - set the PIT timer frequency.  usage: frequency [hz]
;   The kernel reads argv[1] and clamps it (10..2000 Hz, default 100).
[bits 32]
[org 0x00000000]

_start:
    mov eax,44              ; sys_setfreq (reads argv[1] in the kernel)
    int 0x80
    ret
