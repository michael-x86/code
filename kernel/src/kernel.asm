; =============================================================================
; kernel.asm — main entry point
; =============================================================================
;
; Boot sequence
; -------------
; The bootloader (kernel/bootloader.asm) does this before jumping to us:
;   1. Enables A20.
;   2. Loads the kernel image from LBA 1..N into physical 0x100000 (1 MB)
;      via INT 13h (LBA or CHS fallback).
;   3. Builds a flat 4 GB GDT with code (0x08) and data (0x10) segments.
;   4. Enters 32-bit protected mode and jumps to 0x00100000.
;   5. Stores the FS disk parameters at physical memory:
;        [0x500] dword  fs_ata_base   (e.g. 0x1F0)
;        [0x504] byte   fs_ata_drive  (0xE0 master, 0xF0 slave)
;        [0x508] dword  fs_base_lba   (LBA of FS region start on disk)
;
; The kernel is assembled with [org 0xC0100000], so every label here
; resolves to a higher-half virtual address. The first 4 MB is also
; identity-mapped, so the very first instructions can use ebp-relative
; addressing to reach the .bss page tables.
;
; Memory layout
; -------------
;   Pre-paging:
;     ebp = physical base of the kernel image
;     esp = stack_top (a 16 KB region in .bss, identity-mapped)
;   Post-paging:
;     ebp = same value (popad restores it from the pushad frame)
;     esp = stack_top (now accessed at the higher-half address)
;     code runs at 0xC0100000+
;
; The dual ebp-relative + absolute addressing is what lets us build
; page tables and turn on paging, then continue executing without a
; long jump. After paging is enabled, the higher-half aliases cover
; all the page tables, so future code can use absolute addresses.
; =============================================================================


[org 0xC0100000]
global start
bits 32


; Pure equates — no storage, no code. Including this first makes the
; rest of the kernel reference them by name without us having to repeat
; the values.
%include "constants.inc"


; =============================================================================
; .text — code
; =============================================================================
section .text


; -----------------------------------------------------------------------------
; start — kernel entry, runs in physical memory before paging is enabled
; -----------------------------------------------------------------------------
; 1. Disable interrupts, set up flat segments. The bootloader already
;    built the GDT, but we re-load the data segment registers to make
;    sure DS/ES/SS/FS/GS all point at the kernel data segment.
; 2. Compute ebp = physical address of the kernel image. We do this
;    with the standard PIC trick: call .get_pc to push the address of
;    the next instruction, pop it into ebp, then subtract the known
;    offset of .get_pc. ebp is now our base for absolute addressing
;    before paging is enabled.
; 3. Set up a temporary stack and save registers.
; 4. Build page tables, install them in CR3, enable paging.
; 5. Far jump to the higher-half copy of ourselves.
; -----------------------------------------------------------------------------
start:
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    call .get_pc
.get_pc:
    pop ebp
    sub ebp, .get_pc

    lea esp, [stack_top + ebp]
    push ebp
    pushad

    call page_mapping
    lea eax, [page_directory + ebp]
    mov cr3, eax
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    mov eax, higher_half
    jmp eax


; -----------------------------------------------------------------------------
; higher_half — first code to run with paging enabled
; -----------------------------------------------------------------------------
; We re-establish the kernel's segment layout, restore ebp from the
; pushad frame we set up in start, capture the physical bounds of the
; kernel image so reserve_kernel_pages can mark those frames used,
; then switch to the virtual stack and call kernel_main.
; -----------------------------------------------------------------------------
higher_half:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; ebp is the 32-bit value we pushed in start. pushad pushed 8 more
    ; registers above it, so it's at [esp + 32].
    mov ebp, [esp + 32]
    lea eax, [start + ebp]
    mov [kernel_phys_start_var], eax
    lea eax, [page_bitmap + 32768 + ebp]
    mov [kernel_phys_end_var], eax

    call reserve_kernel_pages

    popad
    add esp, 4
    mov esp, stack_top
    mov ebp, esp
    jmp kernel_main


; -----------------------------------------------------------------------------
; kernel_main — the real entry point, runs at 0xC0100000+ with paging on
; -----------------------------------------------------------------------------
; Build the IDT, install the IRQ and syscall gates, remap the PIC,
; program the PIT, initialize the cwd, clear the screen, paint the
; banner, bring up the FS metadata, set the initial cursor position,
; initialize the three task stacks, and finally iret into task 0.
; -----------------------------------------------------------------------------
kernel_main:
    call build_idt
    call set_irq0
    call set_irq1
    call set_syscall
    call set_irq12
    lidt [idt_descriptor]
    call pic_remap
    call set_freq                        ; 100 Hz PIT

    ; Snapshot the BIOS text font (for graphics-mode glyphs and for restoring
    ; text after Mode 13h) and bring up the PS/2 mouse (IRQ12).
    call gfx_save_font
    call mouse_init

    ; Initialize cwd_buf to "/".
    mov edi, cwd_buf
    mov ecx, CWD_BUF_SIZE
    xor eax, eax
    rep stosb
    mov byte [cwd_buf], '/'

    call cls
    call banner

    ; Lightweight FS bring-up: load superblock only. The bitmap is
    ; loaded by the first write path that needs it.
    call ensure_fs_ready

    ; Apply /etc/config (keyboard layout, timezone, date/time formats)
    ; now that the FS is readable but before any input is accepted.
    call load_config

    mov eax, VGA_COLS * 4
    mov [cursor_pos], eax
    call newline
    call prompt

    call init_tasks
    mov dword [current_task], 0
    mov esp, [task0_esp]
    popad
    iretd                                ; into task0_entry with IF=1


; =============================================================================
; Subsystem includes
;
; The order below is significant only for code that has forward references
; within the same source. All labels are resolved at the end of the
; compilation pass, so most of this ordering is by readability: low-level
; subsystems first, then everything that uses them.
; =============================================================================

; --- Pure helpers (string + numeric) ---------------------------------------
%include "lib.inc"

; --- Memory management -------------------------------------------------------
%include "paging.inc"
%include "memory.inc"

; --- Block device and filesystem --------------------------------------------
%include "ata.inc"
%include "fs_core.inc"
%include "vfs.inc"
%include "ecc.inc"

; --- Interrupt subsystem -----------------------------------------------------
%include "idt.inc"
%include "pic.inc"
%include "irq.inc"

; --- User-visible subsystems (depend on the above) --------------------------
%include "vga.inc"
%include "input.inc"
%include "parser.inc"
%include "commands.inc"
%include "shell.inc"
%include "syscall.inc"

%include "graphics.inc"

%include "task.inc"
%include "exec.inc"


; =============================================================================
; .data — initialized mutable state
; =============================================================================
section .data
%include "data.inc"


; =============================================================================
; .bss — zero-initialized state
; =============================================================================
section .bss
%include "bss.inc"
