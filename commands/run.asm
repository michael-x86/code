; run - execute the commands listed in exec.cmd, one per line.
;   Unknown commands print "command not found". The kernel reads exec.cmd
;   (resolved against the current directory) — see sys_run.
[bits 32]
[org 0x00000000]

_start:
    mov eax,50             ; sys_run
    int 0x80
    ret
