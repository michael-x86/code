; ls - list the current directory in 4 columns
[bits 32]
[org 0x00000000]

COLW equ 18              ; column width in characters (4 * 18 = 72 <= 80)

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base          ; runtime delta

    mov dword [ebp+idx],0
    xor ecx,ecx                 ; ecx = current column (0..3), survives int 0x80
.loop:
    lea edi,[ebp+name]
    mov word [edi],0
    mov ebx,[ebp+idx]
    mov eax,13                  ; sys_list_dir(ebx=index, edi=dst) -> eax=type|-1
    int 0x80
    cmp eax,-1
    je .done
    mov edx,eax                 ; edx = type (0=dir), survives int 0x80

    ; --- print the name ---
    lea esi,[ebp+name]
    mov eax,1                   ; sys_print
    int 0x80

    ; --- measure printed length into edi ---
    lea esi,[ebp+name]
    xor edi,edi
.measure:
    cmp byte [esi+edi],0
    je .measured
    inc edi
    jmp .measure
.measured:
    test edx,edx                ; directory -> append '/' and count it
    jnz .padded_or_break
    mov ebx,'/'
    mov eax,39                  ; sys_putchar
    int 0x80
    inc edi

.padded_or_break:
    inc dword [ebp+idx]
    cmp ecx,3                   ; 4th column -> end the row
    je .newrow

    ; pad with spaces up to COLW
    mov eax,COLW
    sub eax,edi
    jle .nopad
    mov ebx,' '
.pad:
    push eax
    mov eax,39                  ; sys_putchar(' ')
    int 0x80
    pop eax
    dec eax
    jnz .pad
.nopad:
    inc ecx
    jmp .loop

.newrow:
    mov eax,3                   ; sys_newline
    int 0x80
    xor ecx,ecx
    jmp .loop

.done:
    test ecx,ecx                ; finish a partial last row
    jz .ret
    mov eax,3
    int 0x80
.ret:
    ret

align 16
name: times 64 db 0
idx:  dd 0
