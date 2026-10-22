get_key:
    mov eax,[kbd_tail]
    cmp eax,[kbd_head]
    je .empty
    mov bl,[kbd_buf+eax]
    inc eax
    and eax,255
    mov [kbd_tail],eax
    mov al,bl
    test al,al
    jz .empty

    ;xor ebx,ebx     ;key code
    ;movsx ebx,al
    ;mov eax,6
    ;int 0x80

    ; --- arrow keys: return values the shell loop handles them ---
    cmp al,0x4B            ; left
    jne .ck_right
    mov al,KEY_LEFT
    ret
.ck_right:
    cmp al,0x4D            ; right
    jne .ck_up
    mov al,KEY_RIGHT
    ret
.ck_up:
    cmp al,0x48            ; up
    jne .ck_down
    mov al,KEY_UP
    ret
.ck_down:
    cmp al,0x50            ; down
    jne .translate
    mov al,KEY_DOWN
    ret
.translate:
    movzx eax,al
    ; -------------------------
    ; choose keymap
    ; -------------------------
    cmp byte [kbd_shift],0
    jne .shifted
.normal:
    mov al,[keymap+eax]
    ret
.shifted:
    mov al,[keymap_shift+eax]
    ret
.empty:
    xor al,al
    ret
.invalid:
    mov al,'?'
    ret

; --- The final curtain call    --- 
; --- powering down the theater ---
irq1:
    pushad
    in al,0x60
    mov bl,al
    cmp bl,0x01              ; ESC -> 800x400 graphics toggle
    je .esc
    cmp bl,41   ;0x3F        ; § -> mode 13h toggle
    je .pnd
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
    cmp byte [gfx_mode],0   ; 800x400 graphics: keys Mandelbrot view
    jne .gfxkeys
    mov eax,[kbd_head]
    mov [kbd_buf+eax],bl
    inc eax
    and eax,255
    mov [kbd_head],eax
    jmp .done

; Mandelbrot zoom/pan 
.gfxkeys:
    cmp bl,0x4B             ; Left
    je .g_left
    cmp bl,0x4D             ; Right
    je .g_right
    cmp bl,0x48             ; Up
    je .g_up
    cmp bl,0x50             ; Down
    je .g_down
    cmp bl,12 ;0x4E         ; keypad +
    je .g_zin
    cmp bl,53 ;0x4A         ; keypad -
    je .g_zout
    cmp bl,0x13             ; R -> reset view
    je .g_reset
    jmp .done               ; any other key: ignore
.g_left:
    call mand_pan_left
    jmp .g_render
.g_right:
    call mand_pan_right
    jmp .g_render
.g_up:
    call mand_pan_up
    jmp .g_render
.g_down:
    call mand_pan_down
    jmp .g_render
.g_zin:
    call mand_zoom_in
    jmp .g_render
.g_zout:
    call mand_zoom_out
    jmp .g_render
.g_reset:
    call mand_reset
.g_render:
    call mandelbrot
    jmp .done
.esc:
    cmp byte [mode13],0     ; ESC does nothing while in mode 13h
    jne .done
    call gfx_toggle
    jmp .done
.pnd:
    cmp byte [mode13],0     
    je .done
    call mode13_off
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


; -------------------------------------------
;  Intel 8259A 
;  IRQ lines -> CPU IRQ vectors
; -------------------------------------------
;  Rewiring the interrupt controller's brain
; -------------------------------------------
