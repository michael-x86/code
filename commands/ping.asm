[bits 32]
[org 0x00000000]

; --- Constants ---
%define ICMP_ECHO_REQ    8

_start:
    ; --- Calculate our Dynamic Base Offset ---
    call .get_base
.get_base:
    pop ebp                 ; ebp = absolute runtime address of .get_base
    sub ebp, .get_base      ; ebp = runtime delta address

    ; --- Fetch target IP string (arg 1) ---
    mov ebx, 1
    lea edi, [ebp + ip_string]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; --- Parse IP string (e.g., "8.8.8.8") into raw 4-byte array ---
    lea esi, [ebp + ip_string]
    lea edi, [ebp + target_ip]
    call parse_ip_string
    jc .bad_ip

    ; --- Print Initial Greeting: "PING 8.8.8.8 with 32 bytes of data:" ---
    lea esi, [ebp + ping_start]
    mov eax,2              ; sys_print
    int 0x80
    lea esi, [ebp + ip_string]
    mov eax,2 
    int 0x80
    lea esi, [ebp + ping_end]
    mov eax,2
    int 0x80

    ; --- Build a real ICMP Header in Memory for Validation ---
    lea edi, [ebp + icmp_packet]
    mov byte [edi], ICMP_ECHO_REQ   ; Type = 8
    mov byte [edi+1], 0             ; Code = 0
    mov word [edi+2], 0             ; Checksum (Zero out first)
    mov word [edi+4], 0xABCD        ; Identifier
    mov word [edi+6], 0x0001        ; Sequence Number

    ; Calculate the 8-byte ICMP Checksum
    mov esi, edi
    mov ecx, 4                      ; 4 words = 8 bytes
    call calculate_checksum
    mov [edi+2], ax                 ; Save real checksum into buffer

    ; --- Simulate Network Latency Delay ---
    ; We loop a bit to simulate a real hardware timing window response
    mov ecx, 0x04000000
.delay:
    dec ecx
    jnz .delay

    ; --- Print Successful Mock Reply ---
    lea esi, [ebp + reply_start]
    mov eax,2
    int 0x80
    lea esi, [ebp + ip_string]
    mov eax,2
    int 0x80
    lea esi, [ebp + reply_end]
    mov eax,2
    int 0x80
    ret

.bad_ip:
    lea esi, [ebp + bad_ip_msg]
    mov eax,2              ; sys_print_err
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret


; =============================================================================
; UTILITY FUNCTIONS
; =============================================================================

; --- parse_ip_string ---
; Parses string representation into 4 raw bytes. Validates bounds (0-255).
parse_ip_string:
    pushad
    mov ecx, 4
.octet_loop:
    xor eax, eax
.char_loop:
    mov bl, [esi]
    cmp bl, '.'
    je .next_octet
    test bl, bl
    jz .end_string
    cmp bl, '0'
    jb .error
    cmp bl, '9'
    ja .error
    
    imul eax, 10
    sub bl, '0'
    movzx ebx, bl
    add eax, ebx
    cmp eax, 255
    ja .error
    
    inc esi
    jmp .char_loop

.next_octet:
    cmp ecx, 1
    je .error
    mov [edi], al
    inc edi
    inc esi
    dec ecx
    jmp .octet_loop

.end_string:
    cmp ecx, 1
    jne .error
    mov [edi], al
    popad
    clc
    ret
.error:
    popad
    stc
    ret

; --- calculate_checksum ---
calculate_checksum:
    push ecx
    push edx
    xor eax, eax
.loop:
    movzx edx, word [esi]
    add eax, edx
    add esi, 2
    loop .loop
.carry_check:
    mov edx, eax
    shr edx, 16
    test edx, edx
    jz .done
    and eax, 0xFFFF
    add eax, edx
    jmp .carry_check
.done:
    not ax
    pop edx
    pop ecx
    ret


; --- Constant Data Section ---
usage_msg   db "usage: ping <ip_address>", 13, 0
bad_ip_msg  db "ping: unknown host or invalid IP layout", 13, 0
ping_start  db "PING ", 0
ping_end    db " with 32 bytes of data: ",13,0
reply_start db "Reply from ",0
reply_end   db ": bytes=32 time=2ms TTL=64",13,13,0

; --- Virtual BSS Section (0 bytes on disk) ---
    align 4
_bss_start:   equ $
target_ip:    equ _bss_start + 0
ip_string:    equ _bss_start + 4
icmp_packet:  equ _bss_start + 68
