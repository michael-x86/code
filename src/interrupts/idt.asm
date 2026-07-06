; IDT construction
build_idt:
    mov edi, idt_start
    mov ecx, 256
.fill:
    mov eax,isr_default
    mov [edi],ax
    mov word [edi+2],0x08
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax, 16
    mov [edi+6],ax
    add edi, 8
    loop .fill
    mov eax, page_fault_isr
    mov ebx, 14
    mov edi, idt_start
    call set_idt_entry
    ret

set_idt_entry:
    push eax
    push edx
    mov edx, eax
    mov word [edi+ebx*8+0], dx
    mov word [edi+ebx*8+2], 0x08
    mov byte [edi+ebx*8+4], 0
    mov byte [edi+ebx*8+5], 10001110b
    shr edx, 16
    mov word [edi+ebx*8+6], dx
    pop edx
    pop eax
    ret
