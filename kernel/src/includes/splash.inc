; =============================================================================
; splash.inc — boot splash image display (VGA Mode 13h, 320x200x256)
; =============================================================================
; Displays the VOSS-HAAS boot splash image for SPLASH_DELAY ticks at 100 Hz
; before showing the banner and starting the shell.
;
; Uses the legacy Mode 13h register set (320x200x256 chain-4 framebuffer at
; physical 0xA0000, already identity-mapped at 0xC00A0000).  This avoids
; the Bochs VBE dispi interface, which some QEMU/configurations lack.
;
; Call from kernel_main after gfx_save_font / mouse_init but before cls /
; banner.  Interrupts must be disabled on entry; this function enables them
; temporarily for the delay and restores the disabled state before returning.
;
; SPLASH_DELAY equ 300   → 3 s  (overridable before including this file)
; =============================================================================


section .rodata

alignb 4
splash_bin:
    incbin "splash.dat"


section .text

; -----------------------------------------------------------------------------
; splash_display — enter Mode 13h, show splash, wait, exit
; -----------------------------------------------------------------------------
splash_display:
    pushad

    ; Switch to VGA Mode 13h (320x200x256 chain-4)
    mov esi, mode13_regs
    call vga_write_regs

    ; Load the 3-3-2 palette so the splash has the right colours
    call gfx_load_palette

    ; Copy splash data directly to the Mode 13h framebuffer at 0xC00A0000
    mov esi, splash_bin
    mov edi, 0xC00A0000          ; identity-mapped alias of physical 0xA0000
    mov ecx, 64000               ; 320 * 200 bytes
    cld
    rep movsb

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

    ; Restore text mode (VGA Mode 03h), reload font, restore text palette
    mov esi, text_regs
    call vga_write_regs
    call gfx_restore_text_palette
    call gfx_load_font

    popad
    ret
