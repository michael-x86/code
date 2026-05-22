irq0:
    pushad
    inc dword [tick_count]
    inc dword [tick_div]
    test dword [tick_div],1
    jnz .no_flag
    mov byte [tick_flag],1
.no_flag:
    mov eax,[current_task]
    inc eax
    cmp eax,3
    jb .save
    xor eax,eax        ; wrap back to 0
.save:
    ; save current esp into correct slot
    cmp dword [current_task],0
    je .was0
    cmp dword [current_task],1
    je .was1
    mov [task2_esp],esp
    jmp .load
.was0:
    mov [task0_esp],esp
    jmp .load
.was1:
    mov [task1_esp],esp
.load:
    mov [current_task],eax
    cmp eax,0
    je .run0
    cmp eax,1
    je .run1
    mov esp,[task2_esp]    ; main 
    jmp .done
.run0:
    mov esp,[task0_esp]
    jmp .done
.run1:
    mov esp,[task1_esp]
.done:
    mov al,0x20
    out 0x20,al
    popad
    iretd

irq1:
    pushad
    in   al,0x60             ; read scancode 
    test al,0x80             ; break code?
    jnz  .done         
    mov  ebx,[kbd_head]
    mov  [kbd_buf+ebx],al
    inc  ebx
    and  ebx,255             
    mov  [kbd_head],ebx
.done:
    mov  al,0x20
    out  0x20,al             ; send EOI to master
    popad
    iretd

; -------------------------------------------
;  Intel 8259A 
; -------------------------------------------
;  Rewiring the interrupt controller's brain
; -------------------------------------------
pic_remap:            
    mov al,0x11
    out 0x20,al
    out 0xA0,al
    mov al,0x20          ; master offset= 0x20
    out 0x21,al
    mov al,0x28          ; slave offset = 0x28
    out 0xA1,al
    mov al,0x04
    out 0x21,al
    mov al,0x02
    out 0xA1,al
    mov al,0x01
    out 0x21,al
    out 0xA1,al
    mov al,0b11111100   
    out 0x21, al
    mov al,0b11111111    ; disable slave completely
    out 0xA1, al
    ret

set_freq:
    ; freq=1193182/divisor
    ; 100 Hz -> divisor=11932
    mov al,0x36
    out 0x43,al
    mov ax,11932
    out 0x40,al          ; low byte
    mov al,ah
    out 0x40,al          ; high byte
    ret
