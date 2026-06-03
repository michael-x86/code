; =============================================================================
; data.inc — mutable global state (initialized, ends up in the binary)
; =============================================================================
;
; Everything in this section is writable AND has an initial value, so the
; loader will copy it from the binary into memory at boot. Rule of thumb:
; if the field is read-only after init, put it in the .rodata section of
; the module that uses it. If it's zero-initialized, put it in bss.inc.
;
; Order is significant only for the syscall_table (the dispatcher indexes
; it by eax) and the IDT descriptor (it has to live near idt_start, but
; the address is taken at link time so the actual location doesn't matter).
; =============================================================================


section .data


; --- TTY / scheduler state ---------------------------------------------------
; tick_count is a free-running 32-bit counter at PIT frequency (100 Hz).
; It is also displayed at the top-right of the screen by IRQ0.
; tick_flag is set every other tick so the shell's task 2 knows when to
; redraw the tick counter without polling 100 times a second.
tick_flag        db  0
tick_count       dd  0
tick_div         dd  0

; cursor_pos is the byte-offset into VGA_TEXT_BUFFER of the next char to
; write. prompt_limit is the cursor_pos recorded right after the "$ " of
; the current prompt — delchar refuses to backspace past it.
cursor_pos       dd  0
prompt_limit     dd  0


; --- Keyboard state -----------------------------------------------------------
; kbd_shift is the current shift-key state (0 = unshifted, 1 = shifted).
; The PS/2 controller generates separate make/break scancodes, and IRQ1
; tracks both. The actual ASCII translation uses one of the keymap_active
; pointers below.
kbd_shift        db  0


; --- Active keymap (set from /etc/config at boot) ---------------------------
; Each keymap is a 256-byte table indexed by scancode. The default points
; at the US keymap in .rodata. /etc/config with `layout=sv` will redirect
; these to the Swedish tables.
keymap_active        dd  keymap
keymap_shift_active  dd  keymap_shift


; --- Config state (populated by load_config from /etc/config) ---------------
; The four fields are kept as integers (0/1/2) rather than strings to keep
; userland code that reads them simple — sys_get_config returns a
; decimal string on the fly.
cfg_timezone    dd  0          ; UTC offset in hours (-12..+14)
cfg_datefmt     dd  0          ; 0=YYYY-MM-DD  1=DD/MM/YYYY  2=MM/DD/YYYY
cfg_timefmt     dd  0          ; 0=24h  1=12h
cfg_layout      dd  0          ; 0=US  1=Swedish


; --- Kernel image bounds (set during boot) ----------------------------------
; The bootloader hands off a flat physical kernel image. We capture its
; start and end so reserve_kernel_pages can mark the frames used.
kernel_phys_start_var  dd  0
kernel_phys_end_var    dd  0


; --- Current process (used by exec_bin and sys_get_proc_info) --------------
; exec_vbase is the base virtual address of the most recently loaded
; userland binary. exec_pages is its page count. exec_name is a copy of
; argv[0] for diagnostics.
exec_vbase   dd  0
exec_pages   dd  0
exec_inode   dd  0
exec_name    times 32 db 0


; --- Command-line parser state ----------------------------------------------
; argc is the count of arguments in argv[]. argv[] is an array of pointers
; INTO cmd_buf — the parser replaces spaces with NULs and points each
; argv slot at the resulting substrings. This means cmd_buf and argv are
; tightly coupled: don't modify cmd_buf while iterating argv.
argc         dd  0
argv         times ARGV_MAX dd 0


; --- Shell edit state --------------------------------------------------------
; cmd_len is the number of bytes currently in cmd_buf (excluding NUL).
; cmd_exec caches the address of the most recently dispatched command
; handler so dispatch_command can return through a small shim.
cmd_len      dd  0
cmd_exec     dd  0


; --- History state ------------------------------------------------------------
; hist_count is the number of valid entries in the circular hist_buf.
; hist_index is the cursor used by hist_back / hist_frwd: 0..count, with
; count meaning "newer than the most recent entry" (empty line).
hist_count   dd  0
hist_index   dd  0


; --- IDT descriptor (operand of lidt) ---------------------------------------
; The descriptor is 6 bytes: 16-bit limit, 32-bit base. We declare it near
; the syscall_table so the address computation in kernel_main is simple.
idt_descriptor:
    dw  idt_end - idt_start - 1
    dd  idt_start


; --- Syscall jump table ------------------------------------------------------
; Indexed by eax at int 0x80. Order MUST match the SYSCALL_* numbers in
; constants.inc. Bounds check is in syscall_isr: any eax >= SYSCALL_COUNT
; returns -1 without dispatch.
;
; Calling convention reminder (all handlers preserve non-output regs):
;   eax = syscall number (in), return value (out)
;   ebx, ecx, edx, esi, edi = arguments
syscall_table:
    dd  sys_putchar          ; 0  : ebx = char
    dd  sys_print            ; 1  : esi = NUL-terminated string
    dd  sys_print_cr         ; 2  : esi = string, CR (0x0D) acts as newline
    dd  sys_newline          ; 3
    dd  sys_cls              ; 4
    dd  sys_print_hex        ; 5  : ebx = value -> 8 hex digits
    dd  sys_print_int        ; 6  : ebx = value -> decimal
    dd  sys_get_key          ; 7  : -> al = ASCII (0 if no key waiting)
    dd  sys_get_tick         ; 8  : -> eax = tick_count
    dd  sys_shutdown         ; 9  : does not return
    dd  sys_read_mem         ; 10 : ebx = addr -> eax = dword at [ebx]
    dd  sys_getcwd           ; 11 : edi = dst buffer
    dd  sys_chdir            ; 12 : esi = path -> 0 / -1
    dd  sys_list_dir         ; 13 : ebx = index, edi = dst -> type (1=file 2=dir) / -1
    dd  sys_get_arg          ; 14 : ebx = index, edi = dst -> 0 / -1
    dd  sys_stat             ; 15 : esi = path, edi = 12-byte info -> 0 / -1
    dd  sys_print_n          ; 16 : esi = buf, ecx = count -> 0
    dd  sys_create           ; 17 : esi = path -> 0 / -1
    dd  sys_write            ; 18 : esi = path, ebx = buf, ecx = n -> 0 / -1
    dd  sys_unlink           ; 19 : esi = path -> 0 / -1
    dd  sys_mkdir            ; 20 : esi = path -> 0 / -1
    dd  sys_rmdir            ; 21 : esi = path -> 0 / -1
    dd  sys_get_ps_info      ; 22 : ebx = 16-byte dst -> 0 / -1
    dd  sys_alloc_pages      ; 23 : ecx = byte count -> eax = vaddr / -1
    dd  sys_free_pages       ; 24 : ebx = vaddr -> 0 / -1
    dd  sys_write_mem        ; 25 : ebx = addr, ecx = byte -> 0
    dd  sys_hex2int          ; 26 : esi = string -> eax
    dd  sys_asc2int          ; 27 : esi = string -> eax
    dd  sys_get_proc_info    ; 28 : ebx = 36-byte dst -> 0 / -1
    dd  sys_itoa             ; 29 : eax = value, edi = dst -> edi past NUL
    dd  sys_get_config       ; 30 : ebx = key 0..3, edi = dst -> 0 / -1
    dd  sys_stack_dump       ; 31
    dd  sys_alloc            ; 32 : ebx = page count -> eax = vaddr / -1
    dd  sys_dealloc          ; 33 : ebx = vaddr -> 0 / -1
    dd  sys_peek             ; 34 : ebx = addr -> eax = byte at [ebx]
    dd  sys_poke             ; 35 : ebx = addr, ecx = byte -> 0
    dd  sys_gfx_enter        ; 36 : switch to Mode 13h (320x200x256)
    dd  sys_gfx_exit         ; 37 : restore text mode 03h + reload font
    dd  sys_gfx_clear        ; 38 : ebx = color -> clear backbuffer
    dd  sys_gfx_pixel        ; 39 : ebx=x, ecx=y, edx=color
    dd  sys_gfx_fillrect     ; 40 : ebx=x, ecx=y, edx=color, esi=w, edi=h
    dd  sys_gfx_rect         ; 41 : ebx=x, ecx=y, edx=color, esi=w, edi=h
    dd  sys_gfx_line         ; 42 : ebx=x0, ecx=y0, edx=x1, esi=y1, edi=color
    dd  sys_gfx_char         ; 43 : ebx=char, ecx=x, edx=y, esi=color
    dd  sys_gfx_string       ; 44 : esi=str, ebx=x, ecx=y, edx=color
    dd  sys_gfx_blit         ; 45 : copy backbuffer -> visible framebuffer
    dd  sys_gfx_info         ; 46 : edi = dst (w, h, bpp) -> 0
    dd  sys_mouse            ; 47 : edi = dst (x, y, buttons) -> 0


; --- VGA register dumps (used by graphics.inc vga_write_regs) ---------------
; Each table is 61 bytes in the canonical order consumed by vga_write_regs:
;   1  Miscellaneous Output (port 0x3C2)
;   5  Sequencer            (index 0x3C4 / data 0x3C5, regs 0..4)
;   25 CRTC                 (index 0x3D4 / data 0x3D5, regs 0..24)
;   9  Graphics Controller  (index 0x3CE / data 0x3CF, regs 0..8)
;   21 Attribute Controller (index+data 0x3C0, regs 0..20)
; Values are the standard register sets for 320x200x256 and 80x25 text.
mode13_regs:
    db 0x63
    db 0x03, 0x01, 0x0F, 0x00, 0x0E
    db 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F
    db 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3
    db 0xFF
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F
    db 0xFF
    db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
    db 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    db 0x41, 0x00, 0x0F, 0x00, 0x00

text_regs:
    db 0x67
    db 0x03, 0x00, 0x03, 0x00, 0x02
    db 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F
    db 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50
    db 0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3
    db 0xFF
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00
    db 0xFF
    db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07
    db 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    db 0x0C, 0x00, 0x0F, 0x08, 0x00
