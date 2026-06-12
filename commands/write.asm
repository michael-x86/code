[bits 32]
[org 0x00000000]

_start:
    ; --- Calculate Dynamic Base Offset ---
    call .get_base
.get_base:
    pop ebp                 
    sub ebp, .get_base     

    ; --- Fetch target filename (arg 1) ---
    mov ebx,1
    lea edi,[ebp + file_arg]
    mov eax,14             ; sys_get_arg
    int 0x80
    cmp eax,-1
    je .usage

    ; Init out_ptr to track the start of the text compilation area
    lea eax,[ebp+text_buf]
    mov [ebp+out_ptr], eax
    
    mov ebx,2              ; Start scanning from argument index 2
.next:
    push ebx
    lea edi,[ebp+word_buf]
    mov eax,14     
    int 0x80
    pop ebx
    cmp eax,-1
    je .done

    ; If it's arg 2, skip injecting a separating space
    cmp ebx, 2
    je .skip_sp
    
    mov edi, [ebp+out_ptr]
    mov byte [edi],' '
    inc edi
    mov [ebp+out_ptr], edi

.skip_sp:
    lea esi,[ebp+word_buf]
    mov edi,[ebp+out_ptr]
.cp:
    mov al,[esi]
    test al,al
    jz .endw
    mov [edi],al
    inc esi
    inc edi
    jmp .cp
.endw:
    mov [ebp+out_ptr],edi
    inc ebx
    jmp .next

.done:
    ; Terminate the entire built string with a \n
    mov edi,[ebp+out_ptr]
    mov byte [edi],0x0A    ; '\n'
    inc edi
    
    ; Compute the (byte) length of the text compiled inside text_buf
    mov ecx,edi
    lea eax,[ebp+text_buf]
    sub ecx,eax            ; ecx = total byte count size

    lea esi,[ebp+file_arg]
    lea ebx,[ebp+text_buf]
    mov eax,18             ; sys_write
    int 0x80
    cmp eax,-1
    je .err
    ret               

.usage:
    lea esi,[ebp+usage_msg]
    mov eax,2              ; sys_print
    int 0x80
    ret
.err:
    lea esi,[ebp + err_msg]
    mov eax,2              ; sys_print
    int 0x80
    ret

usage_msg db "usage: write <file> <text>",13,0
err_msg   db "write: failed",13,0

    align 4
_bss_start:  equ $
out_ptr:     equ _bss_start+0
file_arg:    equ _bss_start+4    ; 64 bytes space
word_buf:    equ _bss_start+68   ; 64 bytes space
text_buf:    equ _bss_start+132  ; Remainder of the mapped memory pool
