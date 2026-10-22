sys_dydx:
    xor dword [on_off],1
    ret

dydx_init:
    pusha
    mov word [on_off],1
    mov byte [xpos],40
    mov byte [ypos],12
    mov byte [dltx],1
    mov byte [dlty],1
    popa
    ret

dydx_step: 
    call dydx_cls 
    call dydx_update
    call dydx_plot
    ret

dydx_plot:
    pusha
    mov al,[xpos]
    mov ah,[ypos]
    call dydx_calc_offset
    mov ah,0x04
    mov al,0x07 
    mov [edi],ax
    popa
    ret


dydx_cls:
    pusha
    mov al,[xpos]
    mov ah,[ypos]
    call dydx_calc_offset
    mov word [edi],0x0720  
    popa
    ret

dydx_update:
    pusha
    mov al,[xpos]
    add al,[dltx]
    cmp al,79
    jg .x_right
    cmp al,0 
    jl .x_left
    jmp .store_x
.x_right:
    mov al,79
    neg byte [dltx]
    jmp .store_x
.x_left:
    mov al,0 
    neg byte [dltx]
.store_x:
    mov [xpos], al
    mov al,[ypos]
    add al,[dlty]
    cmp al,24
    jg .y_bottom
    cmp al,0
    jl .y_top
    jmp .store_y
.y_bottom:
    mov al,24 
    neg byte [dlty]
    jmp .store_y
.y_top:
    mov al,0
    neg byte [dlty]
.store_y:
    mov [ypos],al
    popa
    ret

dydx_calc_offset:
    push ebx
    movzx ebx,ah        
    imul ebx,160        ; y*80*2
    movzx eax,al        
    shl eax,1           ; x*2
    add ebx,eax         
    mov edi,0xC00B8000
    add edi,ebx         ; gotcha
    pop ebx
    ret

on_off: dd 0
xpos    db 40
ypos    db 12
dltx    db 1
dlty    db 1

;---------------------
; tab completions
;---------------------
MAND_W     equ 800               ; render area (an 800x400 (600) bitmap)
MAND_H     equ 400
MAND_ITER  equ 128
MAND_ESC   equ 0x00040000        ; 4.0 in Q16.16 (escape radius^2)
; view is def at runtime 
; [-2.5,1.0] x [-0.875,0.875] 
MAND_STEP0 equ 287         ; scale: 3.5 / 800 (Q16.16), square pixels
MAND_CX0   equ -49040      ; centre x ~= -0.748
MAND_CY0   equ 0           ; centre y = 0

mandelbrot:
    pushad
    ; top-left corner: x0=cx-(W/2)*step, y0=cy-(H/2)*step
    mov eax,[mand_step]
    imul eax,MAND_W/2
    mov ebx,[mand_cx]
    sub ebx,eax
    mov [m_x0],ebx
    mov eax,[mand_step]
    imul eax,MAND_H/2
    mov ebx,[mand_cy]
    sub ebx,eax
    mov [m_y0],ebx
    xor edi,edi               ; py=0
.row:
    mov eax,edi               ; ci=y0+py*step
    imul eax,[mand_step]
    add eax,[m_y0]
    mov [m_ci],eax
    xor esi,esi               ; px=0
.col:
    mov eax,esi               ; cr=x0+px*step
    imul eax,[mand_step]
    add eax,[m_x0]
    mov [m_cr],eax
    mov dword [m_zr],0        ; z=0
    mov dword [m_zi],0
    xor ecx,ecx               ; iteration count
.iter:
    mov eax,[m_zr]            ; zr2=zr*zr
    mov ebx,eax

    ; 32-bit signed fixed-point
    ; eax=(eax*ebx)>>16
    imul ebx
    shrd eax,edx,16

    mov [m_zr2],eax
    mov eax,[m_zi]            ; zi2=zi*zi
    mov ebx,eax

    imul ebx
    shrd eax,edx,16

    mov [m_zi2],eax
    mov eax,[m_zr2]           ; escape if zr2+zi2>4.0
    add eax,[m_zi2]
    cmp eax,MAND_ESC
    jg .escaped
    mov eax,[m_zr]            ; zi=2*zr*zi+ci 
    mov ebx,[m_zi]

    imul ebx
    shrd eax,edx,16
    
    shl eax,1
    add eax,[m_ci]
    mov ebx,eax               ; hold new zi
    mov eax,[m_zr2]           ; zr=zr2-zi2+cr
    sub eax,[m_zi2]
    add eax,[m_cr]
    mov [m_zr],eax
    mov [m_zi],ebx
    inc ecx
    cmp ecx,MAND_ITER
    jl .iter
    xor ebx,ebx               ; bounded->black
    jmp .plot
.escaped:
    mov eax,ecx               ; color=mandel_colors[iter%MAND_NCOL]
    xor edx,edx
    mov ebx,MAND_NCOL
    div ebx
    mov ebx,[mandel_colors+edx*4]
.plot:
    mov eax,esi               ; x=px
    mov ecx,edi               ; y=py
    call gfx_plot
    inc esi
    cmp esi,MAND_W
    jl .col
    inc edi
    cmp edi,MAND_H
    jl .row
    call corners
    popad
    ret

; ---------------------------------------------
; Mandelbrot view controls (irq1).
; ---------------------------------------------
mand_reset:
    mov dword [mand_cx],MAND_CX0
    mov dword [mand_cy],MAND_CY0
    mov dword [mand_step],MAND_STEP0
    ret
mand_zoom_in:          ; 0.5 the scale->2x zoom
    mov eax,[mand_step]
    shr eax,1
    cmp eax,1
    jge .s
    mov eax,1
.s:
    mov [mand_step],eax
    ret
mand_zoom_out:         ; *2 the scale, clamp at 4096
    mov eax,[mand_step]
    shl eax,1
    cmp eax,4096
    jle .s
    mov eax,4096
.s:
    mov [mand_step],eax
    ret
; pan by ~100 pixels 
mand_pan_left:
    mov eax,[mand_step]
    imul eax,100
    sub [mand_cx],eax
    ret
mand_pan_right:
    mov eax,[mand_step]
    imul eax,100
    add [mand_cx],eax
    ret
mand_pan_up:
    mov eax,[mand_step]
    imul eax,100
    sub [mand_cy],eax
    ret
mand_pan_down:
    mov eax,[mand_step]
    imul eax,100
    add [mand_cy],eax
    ret

; ------------------------------------------------------------------
;   Turn on SSE (clear CR0.EM, set CR0.MP, set CR4.OSFXSR|OSXMMEXCPT).
;   Must run at CPL 0. The kernel is otherwise integer-only.
; ------------------------------------------------------------------
sse_enable:
    push eax
    mov eax,cr0
    and eax,0xFFFFFFFB          ; clear EM (bit 2)
    or  eax,0x00000002          ; set MP (bit 1)
    mov cr0,eax
    mov eax,cr4
    or  eax,0x00000600          ; set OSFXSR (9) | OSXMMEXCPT (10)
    mov cr4,eax
    pop eax
    ret

; ------------------------------------------------------------------
;   Mandelbrot on the 800x400x32 LFB using SSE — 4 pixels per
;   iteration loop (packed single-precision floats). 
; ------------------------------------------------------------------
mandel_simd:
    pushad
    call sse_enable
    xor edi,edi                  ; py=0
.row:
    xor esi,esi                  ; px=0 (group of 4)
.col:
    cvtsi2ss xmm2,esi            ; cr=xmin+px*step,then bcast+lane offsets
    mulss xmm2,[sf_step]
    addss xmm2,[sf_xmin]
    shufps xmm2,xmm2,0
    addps xmm2,[sv_lanestep]     ; xmm2=cr for the 4 lanes
    cvtsi2ss xmm3,edi            ; ci=ymin+py*step (same for all 4 lanes)
    mulss xmm3,[sf_step]
    addss xmm3,[sf_ymin]
    shufps xmm3,xmm3,0           ; xmm3=ci
    xorps xmm0,xmm0              ; zr=0
    xorps xmm1,xmm1              ; zi=0
    xorps xmm4,xmm4              ; iter=0
    mov ecx,MAND_ITER
.it:
    movaps xmm5,xmm0             ; zr2=zr*zr
    mulps  xmm5,xmm0
    movaps xmm6,xmm1             ; zi2=zi*zi
    mulps  xmm6,xmm1
    movaps xmm7,xmm5             ; mag=zr2+zi2
    addps  xmm7,xmm6
    cmpleps xmm7,[sv_four]       ; mask=(mag<=4.0) per lane
    movmskps eax,xmm7
    test eax,eax
    jz .esc                      ; all 4 lanes escaped
    andps xmm7,[sv_one]          ; iter++ for lanes still bounded
    addps xmm4,xmm7
    movaps xmm7,xmm0             ; zi=2*zr*zi+ci 
    mulps  xmm7,xmm1
    mulps  xmm7,[sv_two]
    addps  xmm7,xmm3
    subps  xmm5,xmm6             ; zr=zr2-zi2+cr
    addps  xmm5,xmm2
    movaps xmm0,xmm5
    movaps xmm1,xmm7
    dec ecx
    jnz .it
.esc:
    cvttps2dq xmm4,xmm4         ; iteration counts->packed int32
    movaps [simd_iter_buf],xmm4
    xor edx,edx                 ; lane=0..3
.plot:
    push edx
    mov eax,[simd_iter_buf+edx*4]
    cmp eax,MAND_ITER
    jae .black                  ; never escaped -> interior
    xor edx,edx
    mov ecx,MAND_NCOL
    div ecx                     ; edx=iter%MAND_NCOL
    mov ebx,[mandel_colors+edx*4]
    jmp .put
.black:
    xor ebx,ebx
.put:
    pop edx
    mov eax,esi                 ; x=px+lane
    add eax,edx
    mov ecx,edi                 ; y=py
    call gfx_plot            
    inc edx
    cmp edx,4
    jb .plot
    add esi,4                   ; next group of 4 columns
    cmp esi,MAND_W
    jl .col
    inc edi
    cmp edi,MAND_H
    jl .row
    popad
    ret

; ------------------------

