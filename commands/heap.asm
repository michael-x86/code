; heap - list active heap allocations and free space
[bits 32]
[org 0x00000000]

NMAX equ 128        ; must match kernel alloc_table_count

_start:
    pushad
    call .base
.base:
    pop ebp
    sub ebp, .base

    ; --- snapshot the kernel heap table ---
    lea ebx,[ebp+buf]
    mov eax,42                  ; sys_heap_info
    int 0x80
    cmp eax,-1
    je .done

    mov eax,3                   ; newline
    int 0x80

    ; --- list each active allocation base as 0x<hex> ---
    mov ecx,[ebp+buf+4]         ; n active (ecx survives int 0x80)
    xor edx,edx                 ; index (survives int 0x80)
.loop:
    cmp edx,ecx
    jae .free
    lea eax,[ebp+buf+8]
    mov ebx,[eax+edx*4]         ; base address
    cmp ebx,ebp                 ; skip our own binary's allocation
    je .skip
    lea esi,[ebp+hexpre]        ; "0x"  (ebx preserved across int 0x80)
    mov eax,2
    int 0x80
    mov eax,5                   ; sys_print_hex (8 hex digits, ebx)
    int 0x80
    mov eax,3                   ; newline
    int 0x80
.skip:
    inc edx
    jmp .loop

    ; --- free heap bytes ---
.free:
    mov ebx,[ebp+buf]           ; free bytes
    mov eax,6                   ; sys_print_int (decimal)
    int 0x80
    lea esi,[ebp+freemsg]
    mov eax,2                   ; print (CR -> newline)
    int 0x80
.done:
    popad
    ret

hexpre:  db "0x",0
freemsg: db " bytes free",13,0

section .bss
alignb 4
buf: resb 8 + NMAX*4
