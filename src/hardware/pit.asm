;----  IRQ 0 (PIT Timer)  ----
set_irq0:
    mov eax,irq0
    mov edx,eax
    mov edi,idt_start+0x20*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr edx,16
    mov word [edi+6],dx
    ret

irq0:
    pushad
    inc dword [tick_count]
    mov eax,[tick_count]
    xor edx,edx
    mov ebx,[hz]
    div ebx
    test edx,edx
    jnz .skip_epoch
    inc dword [boot_epoch]
.skip_epoch:
    mov ecx,[current_task]
    mov [task_esps+ecx*4],esp
    inc ecx
    cmp ecx,3
    jb .set_next
    xor ecx,ecx
.set_next:
    mov [current_task],ecx
    mov esp,[task_esps+ecx*4]
.done:
    mov al, 0x20
    out 0x20, al
    popad
    iretd
