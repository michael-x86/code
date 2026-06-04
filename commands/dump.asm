; dump — hex dump of a memory region.  usage:  dump <hexaddr> <hexcount>
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

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
    int 0x80
    mov [ebp + addr_save], eax  ; save start address

    ; --- Fetch count argument ---
    mov ebx, 2
    lea edi, [ebp + arg_buf2]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; Parse hex count via sys_hex2int
    lea esi, [ebp + arg_buf2]
    mov eax, 26             ; sys_hex2int
    int 0x80
    mov ecx, eax            ; ecx = byte count
    test ecx, ecx
    jz .done

    mov edi, [ebp + addr_save]  ; restore start address

    ; --- Print header ---
    lea esi, [ebp + hdr_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80

    ; --- Dump loop: 16 bytes per line ---
.dump_line:
    cmp ecx, 0
    jle .done

    ; Print address prefix "0xXXXXXXXX  "
    mov al, '0'
    call putchar
    mov al, 'x'
    call putchar
    mov eax, edi
    call print_hex_dword
    mov al, ' '
    call putchar
    mov al, ' '
    call putchar

    ; Save count for this line
    mov edx, ecx
    cmp edx, 16
    jle .line_ok
    mov edx, 16
.line_ok:

    ; Print hex bytes
    push ecx
    mov ecx, edx
.hex_loop:
    push ecx
    movzx ebx, byte [edi]
    mov eax, ebx
    call print_hex_byte
    mov al, ' '
    call putchar
    inc edi
    pop ecx
    loop .hex_loop

    ; Pad remaining columns if line < 16 bytes
    pop ecx
    mov eax, 16
    sub eax, edx
    jle .no_pad
    mov ecx, eax
.pad_loop:
    push ecx
    mov al, ' '
    call putchar
    call putchar
    call putchar
    pop ecx
    loop .pad_loop
.no_pad:

    ; Print ASCII representation
    mov al, ' '
    call putchar
    mov al, '|'
    call putchar

    push ecx
    mov ecx, edx
    mov esi, edi
    sub esi, edx            ; rewind to start of line
.ascii_loop:
    lodsb
    cmp al, 32
    jb .dot
    cmp al, 126
    ja .dot
    jmp .out_ch
.dot:
    mov al, '.'
.out_ch:
    call putchar
    loop .ascii_loop
    pop ecx

    mov al, '|'
    call putchar

    ; Newline
    mov eax, 3              ; sys_newline
    int 0x80

    sub ecx, edx
    jmp .dump_line

.done:
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret

; --- print_hex_byte: print byte in al as two hex digits ---
print_hex_byte:
    push eax
    mov al, ah
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
    jmp putchar

; --- print_hex_dword: print dword in eax as 8 hex digits ---
print_hex_dword:
    push eax
    push ecx
    mov ecx, 8
.hloop:
    rol dword [esp + 4], 4
    mov al, byte [esp + 4]
    and al, 0x0F
    cmp al, 9
    jbe .num2
    add al, 55
    jmp .out2
.num2:
    add al, '0'
.out2:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

; --- putchar: print character in al via syscall ---
putchar:
    mov ebx, eax
    mov eax, 0              ; sys_putchar
    int 0x80
    ret

usage_msg: db "usage: dump <hexaddr> <hexcount>", 13, 0
hdr_msg:   db "ADDRESS          00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  |ASCII|", 13, 0

section .bss
alignb 4
arg_buf:   resb 32
arg_buf2:  resb 32
addr_save: resd 1
