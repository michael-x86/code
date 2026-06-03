; =============================================================================
; graphics.inc — VGA Mode 13h (320x200x256) graphics engine + PS/2 mouse
; =============================================================================
;
; The OS boots and runs in VGA text mode 03h. A userland program (a game,
; or a graphical TUI) calls sys_gfx_enter to switch to Mode 13h at runtime,
; draws via the gfx_* primitives, and calls sys_gfx_exit to return to text
; mode. No BIOS and no bootloader change are involved: the mode switch is
; done by writing the VGA register set directly (vga_write_regs).
;
; Display model
; -------------
;   - Mode 13h is a linear (chain-4) 320x200x256 framebuffer at physical
;     0xA0000, reached at the higher-half alias 0xC00A0000 (already mapped
;     by identity_page_table). Pixel (x,y) is at byte offset y*320 + x.
;   - All primitives draw into gfx_backbuf (an off-screen BSS buffer).
;     gfx_blit copies the whole backbuffer to the visible framebuffer in
;     one pass, giving flicker-free updates.
;   - gfx_enter loads a fixed 3-3-2 palette (color index RRRGGGBB encodes
;     its own RGB), so userland color values are deterministic.
;
; Font handling
; -------------
;   Mode 13h's chain-4 writes clobber the BIOS text font stored in VGA
;   plane 2. gfx_save_font snapshots the 256x16 font into font_save while
;   still in text mode at boot; gfx_exit restores it with gfx_load_font so
;   the shell text is intact again. The same snapshot is the glyph source
;   for gfx_char / gfx_string in graphics mode.
;
; Mouse
; -----
;   mouse_init brings up the PS/2 auxiliary device and enables IRQ12.
;   irq12 decodes the 3-byte packet into mouse_x / mouse_y / mouse_buttons,
;   clamped to the 320x200 screen. sys_mouse exposes the state to userland.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; vga_write_regs — program a full VGA register set
;   in: esi = pointer to a 61-byte dump (1 misc, 5 seq, 25 crtc, 9 gc, 21 ac)
;
; Writes, in order: Miscellaneous Output, the five Sequencer registers, the
; 25 CRTC registers (after clearing the CRTC[0x11] protect bit so 0..7 take
; effect), the nine Graphics Controller registers, and the 21 Attribute
; Controller registers, then re-enables video output (PAS bit).
; -----------------------------------------------------------------------------
vga_write_regs:
    pushad

    ; Miscellaneous Output register (write-only at 0x3C2)
    lodsb
    mov dx, 0x3C2
    out dx, al

    ; Sequencer registers 0..4 (index 0x3C4, data 0x3C5)
    xor bl, bl
    mov ecx, 5
.seq:
    mov dx, 0x3C4
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    inc bl
    loop .seq

    ; Unlock CRTC: clear the protect bit (bit 7) of CRTC index 0x11 so the
    ; horizontal/vertical timing regs 0..7 can be written.
    mov dx, 0x3D4
    mov al, 0x11
    out dx, al
    inc dx
    in al, dx
    and al, 0x7F
    out dx, al

    ; CRTC registers 0..24 (index 0x3D4, data 0x3D5)
    xor bl, bl
    mov ecx, 25
.crtc:
    mov dx, 0x3D4
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    inc bl
    loop .crtc

    ; Graphics Controller registers 0..8 (index 0x3CE, data 0x3CF)
    xor bl, bl
    mov ecx, 9
.gc:
    mov dx, 0x3CE
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    inc bl
    loop .gc

    ; Attribute Controller registers 0..20 (index AND data share port 0x3C0;
    ; reading 0x3DA first resets the index/data flip-flop).
    xor bl, bl
    mov ecx, 21
.ac:
    mov dx, 0x3DA
    in al, dx
    mov dx, 0x3C0
    mov al, bl
    out dx, al
    lodsb
    out dx, al
    inc bl
    loop .ac

    ; Re-enable video output: set the Palette Address Source bit (0x20).
    mov dx, 0x3DA
    in al, dx
    mov dx, 0x3C0
    mov al, 0x20
    out dx, al

    popad
    ret


; -----------------------------------------------------------------------------
; gfx_font_access_on / gfx_font_access_off — switch VGA memory access to/from
; linear plane-2 mode so the character generator font can be read/written.
; -----------------------------------------------------------------------------
gfx_font_access_on:
    push eax
    push edx
    mov dx, 0x3C4               ; Sequencer
    mov ax, 0x0100             ; reg 0 = 0x01 (sync reset)
    out dx, ax
    mov ax, 0x0402             ; reg 2 = 0x04 (write plane 2)
    out dx, ax
    mov ax, 0x0704             ; reg 4 = 0x07 (sequential, odd/even off)
    out dx, ax
    mov ax, 0x0300             ; reg 0 = 0x03 (end reset)
    out dx, ax
    mov dx, 0x3CE              ; Graphics Controller
    mov ax, 0x0204             ; reg 4 = 0x02 (read plane 2)
    out dx, ax
    mov ax, 0x0005             ; reg 5 = 0x00 (write mode 0)
    out dx, ax
    mov ax, 0x0006             ; reg 6 = 0x00 (map 0xA0000, 128K, graphics)
    out dx, ax
    pop edx
    pop eax
    ret

gfx_font_access_off:
    push eax
    push edx
    mov dx, 0x3C4               ; Sequencer — back to text defaults
    mov ax, 0x0100             ; reg 0 = 0x01 (sync reset)
    out dx, ax
    mov ax, 0x0302             ; reg 2 = 0x03 (planes 0 and 1)
    out dx, ax
    mov ax, 0x0304             ; reg 4 = 0x03 (odd/even addressing)
    out dx, ax
    mov ax, 0x0300             ; reg 0 = 0x03 (end reset)
    out dx, ax
    mov dx, 0x3CE              ; Graphics Controller — text defaults
    mov ax, 0x0004             ; reg 4 = 0x00 (read plane 0)
    out dx, ax
    mov ax, 0x1005             ; reg 5 = 0x10 (odd/even)
    out dx, ax
    mov ax, 0x0E06             ; reg 6 = 0x0E (map 0xB8000, 32K, alpha)
    out dx, ax
    pop edx
    pop eax
    ret


; -----------------------------------------------------------------------------
; gfx_save_font — snapshot the 256x16 text font from VGA plane 2 into
; font_save. Must run in text mode (called once at boot).
; -----------------------------------------------------------------------------
gfx_save_font:
    pushad
    call gfx_font_access_on
    mov esi, GFX_FB_VIRT        ; plane-2 font data at 0xC00A0000
    mov edi, font_save
    mov ecx, 256
.char:
    push ecx
    mov ecx, FONT_HEIGHT        ; copy 16 bytes of the glyph
    rep movsb
    add esi, 32 - FONT_HEIGHT   ; advance to the next 32-byte glyph slot
    pop ecx
    loop .char
    call gfx_font_access_off
    mov byte [font_saved], 1
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_load_font — write font_save back into VGA plane 2 (restores text glyphs
; after Mode 13h corrupted them).
; -----------------------------------------------------------------------------
gfx_load_font:
    pushad
    cmp byte [font_saved], 0
    je .done
    call gfx_font_access_on
    mov esi, font_save
    mov edi, GFX_FB_VIRT
    mov ecx, 256
.char:
    push ecx
    mov ecx, FONT_HEIGHT
    rep movsb
    add edi, 32 - FONT_HEIGHT
    pop ecx
    loop .char
    call gfx_font_access_off
.done:
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_load_palette — program all 256 DAC entries with a fixed 3-3-2 palette.
;
; A color index encodes its own RGB value:  bits 7-5 = red, 4-2 = green,
; 1-0 = blue (RRRGGGBB). Each 3-bit field maps to a 6-bit DAC value via *9
; (0->0, 7->63); the 2-bit blue field via *21 (0->0, 3->63). This makes the
; palette deterministic for userland: e.g. 0x00=black, 0xFF=white,
; 0xE0=red, 0x1C=green, 0x03=blue, 0xFC=yellow.
;
; The DAC auto-increments its write index after every R,G,B triple, so we
; set the start index once (port 0x3C8) and stream 256*3 bytes to 0x3C9.
; -----------------------------------------------------------------------------
gfx_load_palette:
    pushad
    mov dx, 0x3C8              ; DAC write-index register
    xor al, al
    out dx, al                ; start at index 0
    mov dx, 0x3C9             ; DAC data register (R, G, B per write)
    xor ecx, ecx             ; ecx = color index 0..255
.next:
    mov eax, ecx              ; red   = ((i >> 5) & 7) * 9
    shr eax, 5
    and eax, 7
    imul eax, eax, 9
    out dx, al
    mov eax, ecx              ; green = ((i >> 2) & 7) * 9
    shr eax, 2
    and eax, 7
    imul eax, eax, 9
    out dx, al
    mov eax, ecx              ; blue  = (i & 3) * 21
    and eax, 3
    imul eax, eax, 21
    out dx, al
    inc ecx
    cmp ecx, 256
    jb .next
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_enter — switch to Mode 13h and present a cleared screen
; -----------------------------------------------------------------------------
gfx_enter:
    pushad
    mov esi, mode13_regs
    call vga_write_regs
    call gfx_load_palette       ; deterministic 3-3-2 256-color palette
    mov byte [gfx_mode], 1
    xor ebx, ebx               ; clear backbuffer to color 0
    call gfx_clear_impl
    call gfx_blit
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_exit — restore text mode 03h, reload the font, clear the text screen
; -----------------------------------------------------------------------------
gfx_exit:
    pushad
    mov esi, text_regs
    call vga_write_regs
    call gfx_load_font
    mov byte [gfx_mode], 0
    call cls
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_clear_impl — fill the backbuffer with a color
;   in: ebx = color (low byte)
; -----------------------------------------------------------------------------
gfx_clear_impl:
    push eax
    push ecx
    push edx
    push edi
    movzx eax, bl
    mov ah, al
    mov edx, eax
    shl eax, 16
    or  eax, edx               ; eax = color repeated in all four bytes
    mov edi, gfx_backbuf
    mov ecx, GFX_FB_SIZE / 4
    rep stosd
    pop edi
    pop edx
    pop ecx
    pop eax
    ret


; -----------------------------------------------------------------------------
; gfx_pixel — plot one pixel into the backbuffer (clipped)
;   in: ebx = x, ecx = y, edx = color
; -----------------------------------------------------------------------------
gfx_pixel:
    push eax
    push edi
    cmp ebx, GFX_WIDTH          ; unsigned: also rejects negative (huge)
    jae .done
    cmp ecx, GFX_HEIGHT
    jae .done
    mov eax, ecx
    imul eax, GFX_WIDTH
    add eax, ebx
    mov edi, gfx_backbuf
    add edi, eax
    mov al, dl
    mov [edi], al
.done:
    pop edi
    pop eax
    ret


; -----------------------------------------------------------------------------
; gfx_fillrect — filled rectangle, clipped to the screen
;   in: ebx = x, ecx = y, edx = color, esi = w, edi = h
; -----------------------------------------------------------------------------
gfx_fillrect:
    pushad
    mov [fr_x], ebx
    mov [fr_color], edx
    mov [fr_w], esi
    mov eax, ecx
    add eax, edi
    mov [fr_yend], eax          ; y_end = y + h
.row:
    mov eax, [fr_yend]
    cmp ecx, eax
    jge .done
    cmp ecx, 0
    jl .next
    cmp ecx, GFX_HEIGHT
    jae .next
    mov ebx, [fr_x]
    cmp ebx, 0
    jl .next
    cmp ebx, GFX_WIDTH
    jae .next
    mov eax, GFX_WIDTH          ; available width = WIDTH - x
    sub eax, ebx
    mov edx, [fr_w]             ; run = min(w, available)
    cmp edx, eax
    jle .have_run
    mov edx, eax
.have_run:
    cmp edx, 0
    jle .next
    mov eax, ecx                ; dest = backbuf + y*WIDTH + x
    imul eax, GFX_WIDTH
    add eax, ebx
    mov edi, gfx_backbuf
    add edi, eax
    mov eax, [fr_color]         ; al = color byte for rep stosb
    push ecx
    mov ecx, edx                ; run length
    rep stosb
    pop ecx
.next:
    inc ecx
    jmp .row
.done:
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_rect — rectangle outline (1px), built from four gfx_fillrect calls
;   in: ebx = x, ecx = y, edx = color, esi = w, edi = h
; -----------------------------------------------------------------------------
gfx_rect:
    pushad
    ; top edge: (x, y, w, 1)
    push edi
    mov edi, 1
    call gfx_fillrect
    pop edi
    ; bottom edge: (x, y+h-1, w, 1)
    push ecx
    add ecx, edi
    dec ecx
    push edi
    mov edi, 1
    call gfx_fillrect
    pop edi
    pop ecx
    ; left edge: (x, y, 1, h)
    push esi
    mov esi, 1
    call gfx_fillrect
    pop esi
    ; right edge: (x+w-1, y, 1, h)
    push ebx
    add ebx, esi
    dec ebx
    push esi
    mov esi, 1
    call gfx_fillrect
    pop esi
    pop ebx
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_line — Bresenham line (all octants)
;   in: ebx = x0, ecx = y0, edx = x1, esi = y1, edi = color
; -----------------------------------------------------------------------------
gfx_line:
    pushad
    mov [gfx_lx0], ebx
    mov [gfx_ly0], ecx
    mov [gfx_lx1], edx
    mov [gfx_ly1], esi
    mov [gfx_lcolor], edi

    ; dx = abs(x1-x0); sx = sign(x1-x0)
    mov eax, [gfx_lx1]
    sub eax, [gfx_lx0]
    mov ebx, 1
    test eax, eax
    jge .dx_pos
    neg eax
    mov ebx, -1
.dx_pos:
    mov [gfx_ldx], eax
    mov [gfx_lsx], ebx

    ; dy = -abs(y1-y0); sy = sign(y1-y0)
    mov eax, [gfx_ly1]
    sub eax, [gfx_ly0]
    mov ebx, 1
    test eax, eax
    jge .dy_pos
    neg eax
    mov ebx, -1
.dy_pos:
    neg eax
    mov [gfx_ldy], eax
    mov [gfx_lsy], ebx

    mov eax, [gfx_ldx]          ; err = dx + dy
    add eax, [gfx_ldy]
    mov [gfx_lerr], eax
.loop:
    mov ebx, [gfx_lx0]
    mov ecx, [gfx_ly0]
    mov edx, [gfx_lcolor]
    call gfx_pixel
    mov eax, [gfx_lx0]
    cmp eax, [gfx_lx1]
    jne .step
    mov eax, [gfx_ly0]
    cmp eax, [gfx_ly1]
    je .done
.step:
    mov eax, [gfx_lerr]
    add eax, eax               ; e2 = 2*err
    cmp eax, [gfx_ldy]         ; if e2 >= dy: err += dy; x0 += sx
    jl .skip_x
    mov ecx, [gfx_lerr]
    add ecx, [gfx_ldy]
    mov [gfx_lerr], ecx
    mov ecx, [gfx_lx0]
    add ecx, [gfx_lsx]
    mov [gfx_lx0], ecx
.skip_x:
    cmp eax, [gfx_ldx]         ; if e2 <= dx: err += dx; y0 += sy
    jg .skip_y
    mov ecx, [gfx_lerr]
    add ecx, [gfx_ldx]
    mov [gfx_lerr], ecx
    mov ecx, [gfx_ly0]
    add ecx, [gfx_lsy]
    mov [gfx_ly0], ecx
.skip_y:
    jmp .loop
.done:
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_char — render an 8x16 glyph from font_save into the backbuffer
;   in: ebx = char, ecx = x, edx = y, esi = color
; -----------------------------------------------------------------------------
gfx_char:
    pushad
    mov [gfx_cx], ecx
    mov [gfx_cy], edx
    mov [gfx_ccolor], esi
    movzx eax, bl
    shl eax, 4                  ; char * 16
    add eax, font_save
    mov [gfx_glyph], eax
    xor edx, edx                ; row 0..15
.row:
    mov eax, [gfx_glyph]
    movzx ebx, byte [eax + edx] ; row bitmask, bit 7 = leftmost pixel
    xor ecx, ecx                ; col 0..7
.col:
    mov eax, 7
    sub eax, ecx                ; bit index = 7 - col
    bt  ebx, eax
    jnc .skip
    push ecx
    push edx
    mov ebx, [gfx_cx]
    add ebx, ecx               ; x + col
    mov ecx, [gfx_cy]
    add ecx, edx               ; y + row
    mov edx, [gfx_ccolor]
    call gfx_pixel
    pop edx
    pop ecx
    mov eax, [gfx_glyph]        ; reload row bits (ebx was clobbered)
    movzx ebx, byte [eax + edx]
.skip:
    inc ecx
    cmp ecx, FONT_WIDTH
    jl .col
    inc edx
    cmp edx, FONT_HEIGHT
    jl .row
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_string — draw a NUL-terminated string, 8px advance per glyph
;   in: esi = string, ebx = x, ecx = y, edx = color
; -----------------------------------------------------------------------------
gfx_string:
    pushad
    mov [gfx_sx], ebx
    mov [gfx_sy], ecx
    mov [gfx_scolor], edx
.next:
    movzx eax, byte [esi]
    test al, al
    jz .done
    push esi
    mov ebx, eax               ; char
    mov ecx, [gfx_sx]          ; x
    mov edx, [gfx_sy]          ; y
    mov esi, [gfx_scolor]      ; color
    call gfx_char
    pop esi
    add dword [gfx_sx], FONT_WIDTH
    inc esi
    jmp .next
.done:
    popad
    ret


; -----------------------------------------------------------------------------
; gfx_blit — copy the whole backbuffer to the visible framebuffer
; -----------------------------------------------------------------------------
gfx_blit:
    pushad
    mov esi, gfx_backbuf
    mov edi, GFX_FB_VIRT
    mov ecx, GFX_FB_SIZE / 4
    rep movsd
    popad
    ret


; =============================================================================
; PS/2 mouse
; =============================================================================

; -----------------------------------------------------------------------------
; ps2_wait_in — spin until the controller input buffer is empty (safe to
; write a command/byte). ps2_wait_out — spin until the output buffer is full
; (a byte is available to read). Both bounded so a missing device can't hang.
; -----------------------------------------------------------------------------
ps2_wait_in:
    push eax
    push ecx
    mov ecx, 100000
.w:
    in al, 0x64
    test al, 0x02
    jz .ok
    dec ecx
    jnz .w
.ok:
    pop ecx
    pop eax
    ret

ps2_wait_out:
    push eax
    push ecx
    mov ecx, 100000
.w:
    in al, 0x64
    test al, 0x01
    jnz .ok
    dec ecx
    jnz .w
.ok:
    pop ecx
    pop eax
    ret

; -----------------------------------------------------------------------------
; mouse_write — send command byte bl to the mouse (0xD4 prefix routes the
; next data byte to the auxiliary device). mouse_read_ack reads the 0xFA ACK.
; -----------------------------------------------------------------------------
mouse_write:
    call ps2_wait_in
    mov al, 0xD4
    out 0x64, al
    call ps2_wait_in
    mov al, bl
    out 0x60, al
    ret

mouse_read_ack:
    call ps2_wait_out
    in al, 0x60
    ret


; -----------------------------------------------------------------------------
; mouse_init — enable the PS/2 auxiliary device and IRQ12, start streaming
; -----------------------------------------------------------------------------
mouse_init:
    pushad
    ; Enable the auxiliary (mouse) device.
    call ps2_wait_in
    mov al, 0xA8
    out 0x64, al

    ; Read the controller configuration byte, enable IRQ12 (bit 1), and
    ; clear the mouse-clock-disable (bit 5), then write it back.
    call ps2_wait_in
    mov al, 0x20
    out 0x64, al
    call ps2_wait_out
    in al, 0x60
    or  al, 0x02
    and al, 0xDF
    mov bl, al
    call ps2_wait_in
    mov al, 0x60
    out 0x64, al
    call ps2_wait_in
    mov al, bl
    out 0x60, al

    ; Tell the mouse to use default settings, then enable data reporting.
    mov bl, 0xF6
    call mouse_write
    call mouse_read_ack
    mov bl, 0xF4
    call mouse_write
    call mouse_read_ack

    ; Center the cursor and reset packet state.
    mov dword [mouse_x], GFX_WIDTH / 2
    mov dword [mouse_y], GFX_HEIGHT / 2
    mov dword [mouse_buttons], 0
    mov dword [mouse_cycle], 0
    popad
    ret


; -----------------------------------------------------------------------------
; irq12 — PS/2 mouse interrupt: assemble the 3-byte packet, then apply it
; -----------------------------------------------------------------------------
irq12:
    pushad
    in al, 0x60
    mov edx, [mouse_cycle]
    cmp edx, 0
    jne .c1
    test al, 0x08               ; bit 3 of byte 0 is always 1 when in sync
    jz .eoi                     ; out of sync: drop this byte
    mov [mouse_pkt0], al
    mov dword [mouse_cycle], 1
    jmp .eoi
.c1:
    cmp edx, 1
    jne .c2
    mov [mouse_pkt1], al        ; X delta
    mov dword [mouse_cycle], 2
    jmp .eoi
.c2:
    mov [mouse_pkt2], al        ; Y delta
    mov dword [mouse_cycle], 0
    call mouse_apply
.eoi:
    mov al, PIC_EOI             ; EOI to slave, then master (cascade)
    out PIC_SLAVE_CMD, al
    mov al, PIC_EOI
    out PIC_MASTER_CMD, al
    popad
    iretd


; -----------------------------------------------------------------------------
; mouse_apply — fold the completed packet into mouse_x / mouse_y / buttons
; -----------------------------------------------------------------------------
mouse_apply:
    push eax
    push ebx
    movzx eax, byte [mouse_pkt0]
    and eax, 0x07
    mov [mouse_buttons], eax

    movsx ebx, byte [mouse_pkt1]    ; X delta (screen +x = mouse +x)
    mov eax, [mouse_x]
    add eax, ebx
    cmp eax, 0
    jge .x_lo
    xor eax, eax
.x_lo:
    cmp eax, GFX_WIDTH - 1
    jle .x_ok
    mov eax, GFX_WIDTH - 1
.x_ok:
    mov [mouse_x], eax

    movsx ebx, byte [mouse_pkt2]    ; Y delta (screen +y = mouse -y)
    mov eax, [mouse_y]
    sub eax, ebx
    cmp eax, 0
    jge .y_lo
    xor eax, eax
.y_lo:
    cmp eax, GFX_HEIGHT - 1
    jle .y_ok
    mov eax, GFX_HEIGHT - 1
.y_ok:
    mov [mouse_y], eax
    pop ebx
    pop eax
    ret


; -----------------------------------------------------------------------------
; set_irq12 — install the IRQ12 gate at IDT vector 0x2C
; -----------------------------------------------------------------------------
set_irq12:
    mov eax, irq12
    mov edi, idt_start + 0x2C * 8
    mov word [edi], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], IDT_PRESENT
    shr eax, 16
    mov word [edi + 6], ax
    ret


; =============================================================================
; Syscall handlers (entries 36..47 in syscall_table)
;
; All graphics handlers preserve the argument registers (the gfx_* helpers
; pushad/popad) and return 0 in eax.
; =============================================================================

sys_gfx_enter:                  ; 36
    call gfx_enter
    xor eax, eax
    ret

sys_gfx_exit:                   ; 37
    call gfx_exit
    xor eax, eax
    ret

sys_gfx_clear:                  ; 38 : ebx = color
    call gfx_clear_impl
    xor eax, eax
    ret

sys_gfx_pixel:                  ; 39 : ebx=x, ecx=y, edx=color
    call gfx_pixel
    xor eax, eax
    ret

sys_gfx_fillrect:               ; 40 : ebx=x, ecx=y, edx=color, esi=w, edi=h
    call gfx_fillrect
    xor eax, eax
    ret

sys_gfx_rect:                   ; 41 : ebx=x, ecx=y, edx=color, esi=w, edi=h
    call gfx_rect
    xor eax, eax
    ret

sys_gfx_line:                   ; 42 : ebx=x0, ecx=y0, edx=x1, esi=y1, edi=color
    call gfx_line
    xor eax, eax
    ret

sys_gfx_char:                   ; 43 : ebx=char, ecx=x, edx=y, esi=color
    call gfx_char
    xor eax, eax
    ret

sys_gfx_string:                 ; 44 : esi=str, ebx=x, ecx=y, edx=color
    call gfx_string
    xor eax, eax
    ret

sys_gfx_blit:                   ; 45 : present the backbuffer
    call gfx_blit
    xor eax, eax
    ret

sys_gfx_info:                   ; 46 : edi = dst (w, h, bpp)
    mov dword [edi], GFX_WIDTH
    mov dword [edi + 4], GFX_HEIGHT
    mov dword [edi + 8], 8
    xor eax, eax
    ret

sys_mouse:                      ; 47 : edi = dst (x, y, buttons)
    mov eax, [mouse_x]
    mov [edi], eax
    mov eax, [mouse_y]
    mov [edi + 4], eax
    mov eax, [mouse_buttons]
    mov [edi + 8], eax
    xor eax, eax
    ret
