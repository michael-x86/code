; pwd - print working directory
;
; ABI contract (all userland binaries must follow this):
;   1. _start runs after the loader has called us at our actual vaddr.
;   2. We compute our own base address in ebp via the call/pop/sub trick.
;   3. All data references in code use [ebp + offset] or lea reg,[ebp + offset].
;   4. [org 0x00000000] is fine: NASM still computes label offsets correctly
;      relative to the start of the binary; ebp is the runtime vaddr base.
[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    lea edi, [ebp + buf]
    mov eax, 11              ; sys_getcwd(edi)
    int 0x80

    lea esi, [ebp + buf]
    mov eax, 1               ; sys_print
    int 0x80
    mov eax, 3               ; sys_newline
    int 0x80
    ret

; --- BSS-equivalent (in-file zeros; the loader does not zero extra pages) ---
align 4
buf: times 128 db 0
