[bits 32]
[org 0x00000000]

_start:
    call get_base
get_base:
    pop ebp
    sub ebp, get_base

    ; ---- decrypt payload ----
    lea esi, [ebp + payload_start]
    mov ecx, payload_size
    call xor_block

    ; ---- execute payload ----
    call payload_start

    ; ---- re-encrypt payload ----
    lea esi, [ebp + payload_start]
    mov ecx, payload_size
    call xor_block

    ; halt / loop
.hang:
    jmp .hang


; -------------------------------------------------
; XOR block routine (SMC-safe, no memory-to-memory ops)
; -------------------------------------------------
xor_block:
    xor eax, eax
.loop:
    xor byte [esi], 0x34
    inc esi
    loop .loop
    ret


; -------------------------------------------------
; encrypted payload (executed after decrypt)
; MUST NOT overlap with control flow above
; -------------------------------------------------
payload_start:
    mov ebx, 1
    mov eax, 4          ; sys_write
    mov ecx, msg
    mov edx, msg_len
    int 0x80
    ret
payload_end:


; -------------------------------------------------
; data (outside encrypted region)
; -------------------------------------------------
msg db 'hello', 10
msg_len equ $ - msg


; -------------------------------------------------
; compute size of payload block
; -------------------------------------------------
payload_size equ payload_end - payload_start
