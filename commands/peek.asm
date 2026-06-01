; peek — read a byte from a memory address.  usage:  peek <hexaddr>
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- Fetch address argument ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; Parse hex address via sys_hex2int
    lea esi, [ebp + arg_buf]
    mov eax, 26             ; sys_hex2int
    int 0x80                ; eax = address

    ; --- Read byte via sys_read_mem (returns dword) ---
    mov ebx, eax
    mov eax, 10             ; sys_read_mem
    int 0x80

    ; Print result as hex byte
    call print_hex_byte

    ; Newline
    mov eax, 3              ; sys_newline
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret

; --- print_hex_byte: print low byte of eax as two hex digits ---
print_hex_byte:
    push eax
    shr al, 4
    and al, 0x0F
    call print_hex_nibble
    pop eax
    and al, 0x0F
    ; fall through

print_hex_nibble:
    cmp al, 9
    jbe .num
    add al, 55
    jmp .out
.num:
    add al, '0'
.out:
    mov ebx, eax
    mov eax, 0              ; sys_putchar
    int 0x80
    ret

usage_msg: db "usage: peek <hexaddr>", 13, 0

section .bss
alignb 4
arg_buf: resb 32
