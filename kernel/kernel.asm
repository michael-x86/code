; =============================================================================
; kernel.asm — main kernel entry point
; =============================================================================
;
; Boot sequence:
;   1. Set up segments, compute ebp-relative base address
;   2. Build page tables, enable paging, jump to higher-half
;   3. Reserve kernel pages, switch to virtual stack
;   4. Initialize IDT, PIC, PIT, keyboard
;   5. Load filesystem, clear screen, print banner
;   6. Initialize multitasking, start scheduler
; =============================================================================

[org 0xC0100000]

global start
bits 32

; ── Shared constants (equates only) ─────────────────────────────────────────
%include "constants.inc"

; ═════════════════════════════════════════════════════════════════════════════
; .text — code (must be first in binary so bootloader jumps to start)
; ═════════════════════════════════════════════════════════════════════════════
section .text

; ── Kernel entry (physical, pre-paging) ─────────────────────────────────────
start:
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Compute ebp = physical address of .get_pc (for ebp-relative addressing)
    call .get_pc
.get_pc:
    pop ebp
    sub ebp, .get_pc

    ; Set up a temporary physical stack
    lea esp, [stack_top + ebp]
    push ebp
    pushad

    ; Build page directory and tables, then enable paging
    call page_mapping
    lea eax, [page_directory + ebp]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Jump to higher-half virtual address
    mov eax, higher_half
    jmp eax

; ── Higher-half execution (virtual memory active) ───────────────────────────
higher_half:
    mov ebp, [esp + 32]          ; restore ebp from the pushad frame
    lea eax, [start + ebp]
    mov [kernel_phys_start_var], eax
    lea eax, [page_bitmap + 32768 + ebp]
    mov [kernel_phys_end_var], eax

    call reserve_kernel_pages

    ; Switch to virtual stack and call main kernel routine
    popad
    add esp, 4
    mov esp, stack_top
    mov ebp, esp
    jmp kernel_main

; ── Module: paging ──────────────────────────────────────────────────────────
%include "paging.inc"

; ── Module: physical memory allocator ───────────────────────────────────────
%include "memory.inc"

; ── Module: interrupt handling ──────────────────────────────────────────────
%include "interrupt.inc"

; ── Module: syscall interface ───────────────────────────────────────────────
%include "syscall.inc"

; ── Module: virtual filesystem ──────────────────────────────────────────────
%include "vfs.inc"

; ── Module: shell and built-in commands ─────────────────────────────────────
%include "shell.inc"

; ── Module: multitasking ────────────────────────────────────────────────────
%include "task.inc"

; ── Module: userland binary executor ────────────────────────────────────────
%include "exec.inc"

; ── Kernel main ─────────────────────────────────────────────────────────────
kernel_main:
    call build_idt
    call set_irq0
    call set_irq1
    call set_syscall
    lidt [idt_descriptor]
    call pic_remap
    call set_freq                ; 100 Hz PIT

    ; Initialize current working directory
    mov edi, cwd_buf
    mov ecx, CWD_BUF_SIZE
    xor eax, eax
    rep stosb
    mov byte [cwd_buf], '/'

    call cls
    call banner

    ; Lightweight FS init: load base_lba + superblock only (no bitmap).
    ; This ensures /bin lookups and directory listing work without blocking.
    call ensure_fs_ready

    mov eax, VGA_COLS * 4
    mov [cursor_pos], eax
    call newline
    call prompt

    ; Start multitasking
    call init_tasks
    mov dword [current_task], 0
    mov esp, [task0_esp]
    popad
    iretd                        ; context-switch into task0_entry (EFLAGS has IF=1)

; ── Initialized mutable data ────────────────────────────────────────────────
section .data
%include "data.inc"

; ── Zero-initialized data (not emitted in binary) ───────────────────────────
section .bss
%include "bss.inc"

; ═════════════════════════════════════════════════════════════════════════════
; .rodata — read-only data
; ═════════════════════════════════════════════════════════════════════════════
section .rodata

; --- Normal keymap (index = scancode) ----------------------------------------
keymap:
    db 0, 27, '1','2','3','4','5','6','7','8'
    db '9','0','-','=', 8, 9
    db 'q','w','e','r','t','y','u','i'
    db 'o','p','[',']', 13, 0
    db 'a','s','d','f','g','h','j','k'
    db 'l',';', 39, '`', 0, '\'
    db 'z','x','c','v','b','n','m'
    db ',','.','/', 0, '*', 0, ' '
    times 0x3B - ($ - keymap) db 0
    db '<'
    times 256 - ($ - keymap) db 0

; --- Shift keymap ------------------------------------------------------------
keymap_shift:
    db 0, 27, '!','@','#','$','%','^','&','*'
    db '(',')','_','+', 8, 9
    db 'Q','W','E','R','T','Y','U','I'
    db 'O','P','{','}', 13, 0
    db 'A','S','D','F','G','H','J','K'
    db 'L',':','"', '~', 0, '|'
    db 'Z','X','C','V','B','N','M'
    db '<','>','?', 0, '*', 0, ' '
    times 0x3B - ($ - keymap_shift) db 0
    db '<'
    times 256 - ($ - keymap_shift) db 0

; --- Swedish keymap (index = scancode) --------------------------------------
; Physical Swedish layout on PS/2 scancodes:
;   0x0D = +    0x0E = ´ (dead acute)   0x1A = å   0x1B = ¨ (dead diaeresis)
;   0x27 = ö    0x28 = ä                0x29 = §   0x56 = < >
keymap_swe:
    db 0, 27, '1','2','3','4','5','6','7','8'
    db '9','0','+', 0xB4, 8, 9              ; 0x0D=+, 0x0E=´(dead acute)
    db 'q','w','e','r','t','y','u','i'
    db 'o','p', 0xE5, 0xA8, 13, 0           ; 0x1A=å, 0x1B=¨(dead diaeresis)
    db 'a','s','d','f','g','h','j','k'
    db 'l', 0xF6, 0xE4, 0xA7, 0, '\'       ; 0x27=ö, 0x28=ä, 0x29=§
    db 'z','x','c','v','b','n','m'
    db ',','.','-', 0, '*', 0, ' '
    times 0x3B - ($ - keymap_swe) db 0
    db '<'
    times 256 - ($ - keymap_swe) db 0

; --- Swedish shift keymap ----------------------------------------------------
keymap_swe_shift:
    db 0, 27, '!','"', '#', 0xA4, '%', '&', '/', '('
    db ')','=', '?', 0x60, 8, 9             ; 0x0D==, 0x0E=`(dead grave)
    db 'Q','W','E','R','T','Y','U','I'
    db 'O','P', 0xC5, 0xA8, 13, 0           ; 0x1A=Å, 0x1B=¨(dead diaeresis)
    db 'A','S','D','F','G','H','J','K'
    db 'L', 0xD6, 0xC4, 0xB1, 0, '|'       ; 0x27=Ö, 0x28=Ä, 0x29=¹
    db 'Z','X','C','V','B','N','M'
    db ';', ':', '_', 0, '*', 0, ' '
    times 0x3B - ($ - keymap_swe_shift) db 0
    db '>'
    times 256 - ($ - keymap_swe_shift) db 0

; --- Built-in command table: name, 0, handler address ------------------------
cmd_table:
    db "peek",  0
    dd peek_cmd
    db "regs",  0
    dd show_regs
    db "stack", 0
    dd show_stack
    db "clear", 0
    dd cls
    db "quit",  0
    dd shutdown
    db "exit",  0
    dd shutdown
    db "help",  0
    dd help_cmd
    db "echo",  0
    dd echo_cmd
    db "alloc", 0
    dd alloc_cmd
    db "free",  0
    dd free_cmd
    db "heap",  0
    dd heap_cmd
    db "sys",   0
    dd sys_cmd
    db 0       ; end marker

; --- Strings -----------------------------------------------------------------
sys_msg      db "*** OMNI INDUSTRIES UNIFIED OPERATING SYSTEM ***", 0
deadbeef     db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

help_me:
    db 13, "peek  -  display 16 bytes at magic address", 13
    db "regs  -  dump CPU registers", 13
    db "stack -  display top of stack", 13
    db "alloc -  allocate 4KB pages", 13
    db "free  -  release an allocation", 13
    db "heap  -  show heap allocations", 13
    db "clear -  clear screen", 13
    db "sys   -  test int 0x80 syscall", 13
    db "exit  -  shutdown", 13, 13, 0

eax_lbl db "EAX: ", 0
ebx_lbl db "EBX: ", 0
ecx_lbl db "ECX: ", 0
edx_lbl db "EDX: ", 0
esi_lbl db "ESI: ", 0
edi_lbl db "EDI: ", 0
ebp_lbl db "EBP: ", 0
esp_lbl db "ESP: ", 0

out_mem        db "OUT OF MEMORY", 13, 0
alloc_mem      db "heap pointer  : 0x", 0
no_arg         db 13, "usage: alloc <size>", 13, 0
in_bytes       db " bytes free", 13, 0
free_usage_msg db 13, "usage: free <hexaddr>", 13, 0
free_ok_msg    db "memory released", 13, 0
bad_free_msg   db "invalid allocation", 13, 0
pf_msg         db 13, "PAGE FAULT", 13, 0
pf_addr        db "ADDRESS: 0x", 0
virt_mem       db "virtual : $", 0
real_mem       db "reality : $", 0
sys_peek_msg   db "peek = 0x", 0
bin_prefix     db "/bin/", 0
command_nf_msg db 13, "command not found", 13, 0

; --- Config strings ----------------------------------------------------------
config_path    db "/etc/config", 0
bin_path       db "/bin", 0
key_layout     db "layout", 0
key_timezone   db "timezone", 0
key_datefmt    db "datefmt", 0
key_timefmt    db "timefmt", 0
