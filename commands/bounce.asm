[bits 32]
[org 0x00000000]

_start:
    pushad
    
    call .get_base
.get_base:
    pop ebp                  
    sub ebp, .get_base    

.infinite_loop:
    call cls_chr
    call update_chr
    call plot_chr

    call delay_sync

    jmp .infinite_loop

    popad
    ret

; -------------------------------------------------------------------------
; Synchronization / Delay Logic
; -------------------------------------------------------------------------
delay_sync:
    push ecx
    ; Adjust this value based on emulation speed / CPU clock.
    ; 0x000FFFFF  a visible delay on most standard x86 setups.
    mov ecx, 0x000FFFFF
.loop_delay:
    dec ecx
    jnz .loop_delay
    pop ecx
    ret

; -------------------------------------------------------------------------
; Screen & Vector Logic (Position Independent)
; -------------------------------------------------------------------------
plot_chr:
    pushad
    lea eax, [ebp + xpos]
    mov al, [eax]
    lea ebx, [ebp + ypos]
    mov ah, [ebx]
    call calc_offset
    mov ah, 0x04            ; Red color attribute
    mov al, 0x07            ; Bullet/Dot character
    mov [edi], ax
    popad
    ret

cls_chr:
    pushad
    lea eax, [ebp + xpos]
    mov al, [eax]
    lea ebx, [ebp + ypos]
    mov ah, [ebx]
    call calc_offset
    mov word [edi], 0x0720  ; Clear with blank space
    popad
    ret

update_chr:
    pushad
    
    ; Load current X values
    lea ecx, [ebp + xpos]
    lea edx, [ebp + dltx]
    mov al, [ecx]
    add al, [edx]
    
    cmp al, 79
    jg .x_right
    cmp al, 0
    jl .x_left
    jmp .store_x
.x_right:
    mov al, 79
    neg byte [edx]
    jmp .store_x
.x_left:
    mov al, 0
    neg byte [edx]
.store_x:
    mov [ecx], al

    ; Load current Y values
    lea ecx, [ebp + ypos]
    lea edx, [ebp + dlty]
    mov al, [ecx]
    add al, [edx]
    
    cmp al, 24
    jg .y_bottom
    cmp al, 0
    jl .y_top
    jmp .store_y
.y_bottom:
    mov al, 24
    neg byte [edx]
    jmp .store_y
.y_top:
    mov al, 0
    neg byte [edx]
.store_y:
    mov [ecx], al
    
    popad
    ret

calc_offset:
    push ebx
    movzx ebx,ah
    imul ebx,160           ; y*80*2
    movzx eax,al
    shl eax,1              ; x*2
    add ebx,eax
    mov edi,0xC00B8000     
    add edi,ebx            
    pop ebx
    ret

xpos    db 40
ypos    db 12
dltx    db 1
dlty    db 1
