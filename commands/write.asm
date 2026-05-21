; write - write text to a file.  usage:  write <file> <text...>
; Joins argv[2..] with spaces, appends \n, then sys_write.
[bits 32]
[org 0xC0700000]

%define file_arg  0xC0700400
%define word_buf  0xC0700440
%define text_buf  0xC0700480

_start:
    mov ebx, 1
    mov edi, file_arg
    mov eax, 14
    int 0x80
    cmp eax, -1
    je .usage

    mov dword [out_ptr], text_buf
    mov ebx, 2
.next:
    push ebx
    mov edi, word_buf
    mov eax, 14
    int 0x80
    pop ebx
    cmp eax, -1
    je .done

    cmp ebx, 2
    je .skip_sp
    mov edi, [out_ptr]
    mov byte [edi], ' '
    inc edi
    mov [out_ptr], edi
.skip_sp:
    mov esi, word_buf
    mov edi, [out_ptr]
.cp:
    mov al, [esi]
    test al, al
    jz .endw
    mov [edi], al
    inc esi
    inc edi
    jmp .cp
.endw:
    mov [out_ptr], edi
    inc ebx
    jmp .next

.done:
    mov edi, [out_ptr]
    mov byte [edi], 0x0A
    inc edi
    mov ecx, edi
    sub ecx, text_buf

    mov esi, file_arg
    mov ebx, text_buf
    mov eax, 18              ; sys_write
    int 0x80
    cmp eax, -1
    je .err
    ret

.usage:
    mov esi, usage_msg
    mov eax, 2
    int 0x80
    ret
.err:
    mov esi, err_msg
    mov eax, 2
    int 0x80
    ret

usage_msg db "usage: write <file> <text>", 13, 0
err_msg   db "write: failed", 13, 0
out_ptr   dd 0
