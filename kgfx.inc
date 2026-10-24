;-----------------
corners:
    mov eax,400
    mov ecx,200
    mov ebx,0x00FF88FF
    call gfx_plot

    ; Top-Left (White)
    mov eax,0            ; X = 0
    mov ecx,0            ; Y = 0
    mov ebx,0x00FFFFFF   ; Color = White
    call gfx_plot

    ; Top-Right (Red)
    mov eax,799          ; X = 799 (Max Width)
    mov ecx,0            ; Y = 0
    mov ebx,0x00FF0000   ; Color = Red
    call gfx_plot

    ; Bottom-Left (Green)
    mov eax,0            ; X = 0
    mov ecx,399          ; Y = 399 (Max Height)
    mov ebx,0x0000FF00   ; Color = Green
    call gfx_plot

    ; Bottom-Right (Blue)
    mov eax,799          ; X = 799
    mov ecx,399          ; Y = 399
    mov ebx,0x000000FF   ; Color = Blue
    call gfx_plot
    ret

;  VGA plane 2 (text font) linearly @ 0xA000.
vga_plane2_access:
    push eax
    push edx
    mov dx,0x3C4        ; SEQ reset
    mov al,0x00
    out dx,al
    mov dx,0x3C5
    mov al,0x01
    out dx,al
    mov dx,0x3C4        ; map mask=plane 2
    mov al,0x02
    out dx,al
    mov dx,0x3C5
    mov al,0x04
    out dx,al
    mov dx,0x3C4        ; mem mode=ext, sequential, odd/even off
    mov al,0x04
    out dx,al
    mov dx,0x3C5
    mov al,0x07
    out dx,al
    mov dx,0x3C4        ; end reset
    mov al,0x00
    out dx,al
    mov dx,0x3C5
    mov al,0x03
    out dx,al
    mov dx,0x3CE       ; GC read map select = plane 2
    mov al,0x04
    out dx,al
    mov dx,0x3CF
    mov al,0x02
    out dx,al
    mov dx,0x3CE       ; GC mode = 0
    mov al,0x05
    out dx,al
    mov dx,0x3CF
    mov al,0x00
    out dx,al
    mov dx,0x3CE       ; GC misc = map 0xA0000, planar
    mov al,0x06
    out dx,al
    mov dx,0x3CF
    mov al,0x00
    out dx,al
    pop edx
    pop eax
    ret

; 8x16 font (plane 2) -> font_save.
vga_save_font:
    pushad
    call vga_plane2_access
    mov esi,0xC00A0000
    mov edi,font_save
    mov ecx,8192/4
    rep movsd
    mov byte [font_saved],1
    popad
    ret

vga_restore_font:
    pushad
    cmp byte [font_saved],0
    je .done
    call vga_plane2_access
    mov esi,font_save
    mov edi,0xC00A0000
    mov ecx,8192/4
    rep movsd
.done:
    popad
    ret

;   VGA controller for 80x25 text (mode 0x03).
vga_mode3:
    pushad
    mov dx,0x3C2            ; Misc Output
    mov al,0x67
    out dx,al

    mov esi,vga_seq_m3      ; Sequencer 0..4
    xor ecx,ecx
.seq:
    mov dx,0x3C4
    mov al,cl
    out dx,al
    mov dx,0x3C5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,5
    jb .seq

    mov dx,0x3D4           ; clear protect bit
    mov al,0x11
    out dx,al
    mov dx,0x3D5
    mov al,0x0E
    out dx,al

    mov esi,vga_crtc_m3   
    xor ecx,ecx
.crtc:
    mov dx,0x3D4
    mov al,cl
    out dx,al
    mov dx,0x3D5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x19
    jb .crtc

    mov esi,vga_gc_m3    
    xor ecx,ecx
.gc:
    mov dx,0x3CE
    mov al,cl
    out dx,al
    mov dx,0x3CF
    lodsb
    out dx,al
    inc ecx
    cmp ecx,9
    jb .gc

    mov dx,0x3DA          ; reset attribute flip-flop
    in al,dx
    mov esi,vga_ac_m3     
    xor ecx,ecx
.ac:
    mov dx,0x3C0
    mov al,cl
    out dx,al
    lodsb
    out dx,al             
    inc ecx
    cmp ecx,0x15
    jb .ac
    mov dx,0x3C0
    mov al,0x20           ; re-enable video output, lock palette
    out dx,al
    mov dx,0x3C6          ; PEL mask
    mov al,0xFF
    out dx,al
    popad
    ret
; What a pain in the...
vga_seq_m3:  db 0x03,0x00,0x03,0x00,0x02
vga_crtc_m3: db 0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E
             db 0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF
vga_gc_m3:   db 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF
vga_ac_m3:   db 0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B
             db 0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00

; restore text mode.
gfx_off:
    call vbe_off
    call vga_restore_font   ; repaint the 8x16 text font 
    call vga_mode3          ; bring back the 80x25 text 
    mov byte [gfx_mode],0
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    call newline
    call prompt
    ret

; ESC handler
gfx_toggle:
    cmp byte [gfx_mode],0
    jne .leave
    call gfx_on
    ret
.leave:
    call gfx_off
    ret

;--------------------------------------------------------------
; 320x200x256, linear framebuffer at 0xA0000.
; The `mode13` command enters mode 13h; the shell keeps running 
; `setpixel x y c`, `clearpixel x y` and `clear` 
; work from the (invisible) text prompt.  ;-)
;--------------------------------------------------------------

vga_mode13:
    pushad
    mov dx,0x3C2               ; Misc Output
    mov al,0x63
    out dx,al

    mov esi,vga_seq_m13        ; Sequencer 0..4
    xor ecx,ecx
.seq:
    mov dx,0x3C4
    mov al,cl
    out dx,al
    mov dx,0x3C5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,5
    jb .seq

    mov dx,0x3D4              ; unlock CRTC 0-7
    mov al,0x11
    out dx,al
    mov dx,0x3D5
    mov al,0x0E
    out dx,al

    mov esi,vga_crtc_m13       ; CRTC 0..0x18
    xor ecx,ecx
.crtc:
    mov dx,0x3D4
    mov al,cl
    out dx,al
    mov dx,0x3D5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x19
    jb .crtc

    mov esi,vga_gc_m13         ; Graphics Controller 0..8
    xor ecx,ecx
.gc:
    mov dx,0x3CE
    mov al,cl
    out dx,al
    mov dx,0x3CF
    lodsb
    out dx,al
    inc ecx
    cmp ecx,9
    jb .gc

    mov dx,0x3DA             ; reset attribute flip-flop
    in al,dx
    mov esi,vga_ac_m13       ; Attribute Controller 0..0x14
    xor ecx,ecx
.ac:
    mov dx,0x3C0
    mov al,cl
    out dx,al
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x15
    jb .ac
    mov dx,0x3C0
    mov al,0x20              ; unblank
    out dx,al

    call m13_palette
    popad
    ret
; Me made a typo? ;-)
vga_seq_m13:  db 0x03,0x01,0x0F,0x00,0x0E
vga_crtc_m13: db 0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00
              db 0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x40,0x96,0xB9,0xA3
              db 0xFF
vga_gc_m13:   db 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF
vga_ac_m13:   db 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A
              db 0x0B,0x0C,0x0D,0x0E,0x0F,0x41,0x00,0x0F,0x00,0x00

;   256-entry RGB332 palette to the DAC.
;   index bits RRRGGGBB -> 
;   0x00 black, 0xFF white, 0xE0 red, 0x1C green, 0x03 blue.
m13_palette:
    push eax
    push ecx
    push edx
    mov dx,0x3C8           ; DAC write index
    xor al,al
    out dx,al              ; start at colour 0
    mov dx,0x3C9           ; DAC data
    xor ecx,ecx
.loop:
    mov eax,ecx            ; red   = ((i>>5)&7)*9  (0..63)
    shr eax,5
    and eax,7
    imul eax,eax,9
    out dx,al
    mov eax,ecx            ; green = ((i>>2)&7)*9
    shr eax,2
    and eax,7
    imul eax,eax,9
    out dx,al
    mov eax,ecx            ; blue  = (i&3)*21
    and eax,3
    imul eax,eax,21
    out dx,al
    inc ecx
    cmp ecx,256
    jb .loop
    pop edx
    pop ecx
    pop eax
    ret

; fill the framebuffer with color 0
m13_clear:
    push eax
    push ecx
    push edi
    mov edi,M13_FB
    xor eax,eax
    mov ecx,M13_W*M13_H/4
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

;   set one pixel.  eax = x (0..319), ecx = y (0..199), bl = color.
m13_plot:
    push edi
    imul edi,ecx,M13_W       ; y*320
    add edi,eax              ; + x
    add edi,M13_FB
    mov [edi],bl
    pop edi
    ret

;   filled red disc centred on the screen 
;   Scans the bounding box and plots where dx*dx+dy*dy <= r*r.
M13_CX equ M13_W/2
M13_CY equ M13_H/2
M13_R  equ 30
m13_test_circle:
    pushad
    mov esi,-M13_R              ; dy
.yloop:
    mov edi,-M13_R              ; dx
.xloop:
    mov eax,edi
    imul eax,edi                ; dx*dx
    mov ebx,esi
    imul ebx,esi                ; dy*dy
    add eax,ebx
    cmp eax,M13_R*M13_R         ; inside the disc?
    jg .next
    mov eax,M13_CX
    add eax,edi                 ; x=cx+dx
    mov ecx,M13_CY
    add ecx,esi                 ; y=cy+dy
    mov bl,0xE0                 ; RGB332 red
    call m13_plot
.next:
    inc edi
    cmp edi,M13_R
    jle .xloop
    inc esi
    cmp esi,M13_R
    jle .yloop
    popad
    ret

;   Save current 256-entry DAC palette
;   (R,G,B x 256 = 768 bytes)
vga_save_dac:
    pushad
    mov dx,0x3C7            ; DAC read index
    xor al,al
    out dx,al               ; start reading at color 0
    mov dx,0x3C9            ; DAC data
    mov edi,dac_save
    mov ecx,768
.loop:
    in al,dx
    mov [edi],al
    inc edi
    dec ecx
    jnz .loop
    popad
    ret

; write back into the DAC palette.
vga_restore_dac:
    pushad
    mov dx,0x3C8           ; DAC write index
    xor al,al
    out dx,al              ; start writing at color 0
    mov dx,0x3C9           ; DAC data
    mov esi,dac_save
    mov ecx,768
.loop:
    mov al,[esi]
    out dx,al
    inc esi
    dec ecx
    jnz .loop
    popad
    call gfx_off  ;Well... it worked...
    ret

;   enter mode 13h from text mode with a black screen. 
mode13_on:
    cmp byte [gfx_mode],0    ; only from text mode, not from 800x400
    jne .ret
    cmp byte [mode13],0      ; already in mode 13h?
    jne .ret
    cmp byte [font_saved],0
    jne .skipfont
    call vga_save_font       
.skipfont:
    call vga_save_dac         
    call vga_mode13
    call m13_clear
    call m13_test_circle   
    mov byte [mode13],1
.ret:
    ret

;  restore the text console & its palette.
mode13_off:
    call vga_restore_font
    call vga_mode3
    call vga_restore_dac     
    mov byte [mode13],0
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    call newline
    call prompt
    ret

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
pic_remap:            
    pusha 

    in  al,0x21       
    mov ah,al       
    in  al,0xA1      
    push ax           

    mov al,0x11      
    out 0x20,al      
    out 0xA0,al      

    mov al,0x20      
    out 0x21,al
    mov al,0x28      
    out 0xA1,al

    mov al,0x04       
    out 0x21,al
    mov al,0x02      
    out 0xA1,al        

    mov al,0x01         ; 8086/88 (Intel) Mode
    out 0x21,al
    out 0xA1,al

    pop ax              ; AL = Slave mask, AH = Master mask
    out 0xA1,al         ; Restore Slave mask
    mov al,ah           ; Put Master mask back into AL
    out 0x21,al         ; Restore Master mask
    popa
    ret

;   set the PIT timer frequency from the command line.
;   Reads argv[1] (decimal Hz); clamps to 10..2000, default 100
;   Returns eax = Hz.
sys_setfreq:
    push ecx
    push edx
    push esi

    mov edx,100
    mov eax,[argc]
    cmp eax,2
    jl .set_pit

    mov esi,[argv+4]
    call sys_asc2int 

    cmp edx,10
    jl .use_default
    cmp edx,2000
    jg .use_default
    jmp .set_pit
.use_default:
    mov edx,100
.set_pit:
    mov [hz],edx
    mov ecx,edx
    mov eax,1193182
    xor edx,edx
    div ecx                      ; eax  divisor=1193182/hz
    mov ecx,eax
    mov al,0x36
    out 0x43,al
    mov al,cl                    ; divisor low byte
    out 0x40,al
    mov al,ch                    ; divisor high byte
    out 0x40,al
    mov eax,[hz]                 ; return the frequency set
    pop esi
    pop edx
    pop ecx
    ret

;   VGA mode 13h (mode13 command). 
;   F5 or § returns to text mode?
sys_mode13:
    call mode13_on
    xor eax,eax
    ret

;   plot one pixel in mode 13h.
;   ebx = x (0..319), ecx = y (0..199), edx = color (RGB332 index).
sys_setpixel:
    cmp byte [mode13],0
    je .done
    cmp ebx,M13_W
    jae .done
    cmp ecx,M13_H
    jae .done
    mov eax,ebx                  ; m13_plot x in eax
    mov bl,dl                    ; colour in bl 
    call m13_plot                ; eax=x, ecx=y, bl=colour
.done:
    xor eax,eax
    ret

;   clear one pixel in mode 13h.
sys_clearpixel:
    cmp byte [mode13],0
    je .done
    cmp ebx,M13_W
    jae .done
    cmp ecx,M13_H
    jae .done
    mov eax,ebx                  ; x in eax
    xor bl,bl                    ; color 0
    call m13_plot
.done:
    xor eax,eax
    ret

; cls the mode-13h framebuffer to black.
sys_gclear:
    cmp byte [mode13],0
    je .done
    call m13_clear
.done:
    xor eax,eax
    ret

set_freq:
    mov al,0x36
    out 0x43,al
    mov ax,11931        ; al=0x9B (LSB),ah=0x2E (MSB)
    out 0x40,al         
    mov al,ah         
    out 0x40,al       
    ret

sys_msg   db "----------------------------------------"
          db "----------------------------------------"
          db " *** x86 Operating System (EFBEADDE) ***"
          db "       Core timer:                      "
          db "----------------------------------------"
          db "----------------------------------------",0
deadbeef  db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

eax_lbl db "EAX: ",0
ebx_lbl db "EBX: ",0
ecx_lbl db "ECX: ",0
edx_lbl db "EDX: ",0
esi_lbl db "ESI: ",0
edi_lbl db "EDI: ",0
ebp_lbl db "EBP: ",0
esp_lbl db "ESP: ",0

