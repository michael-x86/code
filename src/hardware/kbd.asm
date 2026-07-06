;----  IRQ 1 (Keyboard)  ----
set_irq1:
    mov eax,irq1
    mov edi,idt_start+0x21*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax,16
    mov word [edi+6],ax
    ret

irq1:
    pushad
    in al,0x60
    mov bl,al
    cmp bl,0x2A
    je .shift_on
    cmp bl,0x36
    je .shift_on
    cmp bl,0xAA
    je .shift_off
    cmp bl,0xB6
    je .shift_off
    test bl,0x80
    jnz .done
    mov eax,[kbd_head]
    mov [kbd_buf+eax],bl
    inc eax
    and eax,255
    mov [kbd_head],eax
    jmp .done
.shift_on:
    mov byte [kbd_shift],1
    jmp .done
.shift_off:
    mov byte [kbd_shift],0
.done:
    mov al,0x20
    out 0x20,al
    popad
    iretd
