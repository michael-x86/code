; =============================================================================
; splash.inc — boot splash image display
; =============================================================================
; Displays the VOSS-HAAS boot splash image for SPLASH_DELAY ticks (100 Hz)
; after entering VBE graphics mode, then returns to text mode.
;
; Call from kernel_main after gfx_save_font / mouse_init but before cls /
; banner.  Interrupts must be disabled on entry (they are in kernel_main);
; this function enables them temporarily for the delay and restores the
; disabled state before returning.
;
; SPLASH_DELAY equ 300   → 3 s  (overridable before including this file)
; =============================================================================


section .rodata

%ifndef SPLASH_DELAY
SPLASH_DELAY equ 300        ; 3 seconds at 100 Hz
%endif

alignb 4
splash_bin:
    incbin "splash.dat"


section .text

; -----------------------------------------------------------------------------
; splash_display — enter graphics mode, show splash, wait, exit
; in:  interrupts disabled (cli)
; out: text mode restored, interrupts still disabled
; -----------------------------------------------------------------------------
splash_display:
    pushad

    ; Enter 640x400x256 VBE mode (loads palette, clears + blits backbuffer)
    call gfx_enter

    ; Copy the splash image directly into the off-screen backbuffer
    mov esi, splash_bin
    mov edi, gfx_backbuf
    mov ecx, GFX_FB_SIZE / 4
    rep movsd

    ; Blit backbuffer to the visible VBE framebuffer
    call gfx_blit

    ; Wait SPLASH_DELAY ticks at 100 Hz.  Enable interrupts so the PIT
    ; irq0 handler increments tick_count; HLT stops the CPU until the
    ; next interrupt, then we check whether enough ticks have elapsed.
    mov eax, [tick_count]
    add eax, SPLASH_DELAY
.wait:
    sti
    hlt
    cli
    cmp [tick_count], eax
    jb .wait

    ; Restore text mode (VGA Mode 03h, reload font, clear screen)
    call gfx_exit

    popad
    ret
