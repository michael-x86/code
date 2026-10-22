pci_find_vga:
    push ebx
    push ecx
    push edx
    xor ebx,ebx                      ; device number 0..31
.scan:
    mov eax,0x80000000               ; enable | bus 0 | dev<<11 | func 0 | offset 0
    mov ecx,ebx
    shl ecx,11
    or eax,ecx
    mov dx,0xCF8
    out dx,eax
    mov dx,0xCFC
    in eax,dx
    cmp eax,0x11111234               ; (device 0x1111 << 16) | vendor 0x1234
    je .found
    inc ebx
    cmp ebx,32
    jb .scan
    xor eax,eax                      ; not found
    jmp .done
.found:
    mov eax,0x80000000               ; same device, offset 0x10 (BAR0)
    mov ecx,ebx
    shl ecx,11
    or eax,ecx
    or eax,0x10
    mov dx,0xCF8
    out dx,eax
    mov dx,0xCFC
    in eax,dx
    and eax,0xFFFFFFF0               ; mask BAR flags -> LFB physical base
.done:
    pop edx
    pop ecx
    pop ebx
    ret

;   Map the LFB into virtual memory at LFB_VIRT (PDE 773).
;   Runs once; sets gfx_mapped on success. Leaves flag 0 if no VGA.
gfx_map_lfb:
    pushad
    call pci_find_vga
    test eax,eax
    jz .done                         ; no framebuffer -> stay in text mode
    mov [lfb_phys],eax

    ; PDE[773] -> gfx_page_table (phys = virt - 0xC0000000)
    mov eax,gfx_page_table
    sub eax,0xC0000000
    or eax,3
    mov [page_directory + 773*4],eax

    ; fill PTEs: gfx_page_table[i] = (lfb_phys + i*4096) | present|rw
    mov edi,gfx_page_table
    mov eax,[lfb_phys]
    or eax,3
    mov ecx,GFX_PAGES
.fill:
    mov [edi],eax
    add edi,4
    add eax,4096
    dec ecx
    jnz .fill

    mov eax,cr3                      ; flush TLB
    mov cr3,eax
    mov byte [gfx_mapped],1
.done:
    popad
    ret

;  800x400x32 linear framebuffer enabled.
vbe_set:
    push eax
    push edx
    mov dx,VBE_INDEX        ; ENABLE = 0 (disable while reconfiguring)
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    xor ax,ax
    out dx,ax
    mov dx,VBE_INDEX        ; XRES
    mov ax,1
    out dx,ax
    mov dx,VBE_DATA
    mov ax,GFX_W
    out dx,ax
    mov dx,VBE_INDEX        ; YRES
    mov ax,2
    out dx,ax
    mov dx,VBE_DATA
    mov ax,GFX_H
    out dx,ax
    mov dx,VBE_INDEX        ; BPP
    mov ax,3
    out dx,ax
    mov dx,VBE_DATA
    mov ax,32
    out dx,ax
    mov dx,VBE_INDEX        ; ENABLE = ENABLED | LFB
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    mov ax,0x41
    out dx,ax
    pop edx
    pop eax
    ret

;  disable VBE; QEMU reverts to the VGA text mode.
vbe_off:
    push eax
    push edx
    mov dx,VBE_INDEX
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    xor ax,ax
    out dx,ax
    pop edx
    pop eax
    ret

;   black.
gfx_clear:
    push eax
    push ecx
    push edi
    mov edi,LFB_VIRT
    xor eax,eax
    mov ecx,GFX_W*GFX_H
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

;  eax=x, ecx=y, ebx=color (0x00RRGGBB)
gfx_plot:
    push edx
    push edi
    mov edi,ecx
    imul edi,GFX_PITCH
    mov edx,eax
    shl edx,2
    add edi,edx
    add edi,LFB_VIRT
    mov [edi],ebx
    pop edi
    pop edx
    ret

hires:
    pushad
    ;call mandelbrot    
    call mandel_simd    
    popad
    ret

; ------------------------------------------------------------------
;   Mandelbrot set across the 800x400 LFB using
;   Q16.16 fixed-point. 
; ------------------------------------------------------------------
gfx_on:
    cmp byte [gfx_mapped],1
    je .ready
    call gfx_map_lfb
    cmp byte [gfx_mapped],1
    jne .fail                       ; mapping failed -> stay in text mode
.ready:
    cmp byte [font_saved],0
    jne .skipfont
    call vga_save_font           
.skipfont:
    call vbe_set
    call gfx_clear
    call mand_reset                
    call hires
    mov byte [gfx_mode],1
.fail:
    ret

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

