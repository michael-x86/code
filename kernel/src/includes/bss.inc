; =============================================================================
; bss.inc — zero-initialized data (not emitted in the binary)
; =============================================================================
;
; The .bss section is allocated by the loader (or by us, post-paging) and
; zeroed. We group all zero-initialized state here, in roughly the order
; in which the modules that own each region are initialized. This is a
; readability choice: the layout is otherwise flexible.
;
; Alignment notes:
;   - Page tables and stacks use `alignb 4096` so their physical address
;     fits the alignment requirement of CR3 and the stack-swap code.
;   - The IDT uses `alignb 8` because each gate is 8 bytes.
;   - Buffers and small structs use `alignb 4` to keep fields naturally
;     aligned and avoid unaligned-load penalties.
; =============================================================================


section .bss


; --- Tab completion state (used by shell.inc) -------------------------------
alignb 4
tab_match_count  resd 1       ; number of candidates matching the prefix
tab_single_ptr   resd 1       ; pointer to the only match, if exactly one


; --- Task state (used by task.inc / irq.inc) ---------------------------------
task0_esp        resd 1       ; saved ESP for task 0 (idle)
task1_esp        resd 1       ; saved ESP for task 1 (idle)
task2_esp        resd 1       ; saved ESP for task 2 (shell)
current_task     resd 1       ; 0, 1, or 2 — index of the running task


; --- Keyboard buffer (used by input.inc) ------------------------------------
alignb 4
kbd_head         resd 1       ; producer index (IRQ1 writes here)
kbd_tail         resd 1       ; consumer index (get_key reads here)
kbd_buf          resb KBD_BUF_SIZE


; --- Command-line + history buffers (used by parser.inc / shell.inc) -------
cmd_buf          resb CMD_BUF_SIZE
hist_buf         resb HIST_COUNT_MAX * HIST_RECORD_SIZE


; --- Directory listing scratch (used by commands.inc / ls_cmd) -------------
dir_buf          resb 512
ls_opt_l         resb 1                   ; ls -l (long listing) flag
ls_opt_h         resb 1                   ; ls -h (human-readable sizes) flag


; --- VFS / path state (used by vfs.inc) -------------------------------------
cwd_buf          resb CWD_BUF_SIZE        ; current working directory
resolve_buf      resb CWD_BUF_SIZE        ; scratch for fs_resolve output
path_buf         resb CWD_BUF_SIZE        ; scratch for parent path during create/mkdir

; --- du / df scratch (used by commands.inc) ---------------------------------
du_path          resb 256                 ; running path during the du tree walk
df_total         resd 1                   ; df: total bytes
df_used          resd 1                   ; df: used bytes
df_free          resd 1                   ; df: free bytes

; --- Scratch slots used by individual syscall handlers -----------------------
; tmp_dst is sys_list_dir's destination buffer pointer (saved across the
; ATA read, since several registers are clobbered by the read).
tmp_dst          resd 1
tmp_left         resd 1


; --- Legacy FS persist buffer (kept for binary-compat, not actively used) --
; gen_fs.py no longer embeds the old 68-byte record format, so this region
; is reserved but unreferenced. Remove in a future cleanup pass.
persist_buf      resb FS_PERSIST_BUF_SIZE


; --- ATA PIO state (used by drivers/ata.inc) ----------------------------------------
; The bootloader writes fs_ata_base, fs_ata_drive, and fs_base_lba to
; physical 0x500, 0x504, 0x508. We read them at boot and cache them here.
; ata_port_base is the same value as fs_ata_base; it lives in a separate
; slot so ata_read/ata_write can do `mov dx, [ata_port_base]` without
; re-loading fs_ata_base on every port access.
alignb 4
fs_ata_base      resd 1                   ; ATA port base
fs_ata_drive     resb 1                   ; drive select byte
fs_base_lba      resd 1                   ; disk LBA where the FS region starts
fs_ready         resb 1                   ; 1 once the superblock has been loaded
ata_port_base    resd 1                   ; hot cache of fs_ata_base

; disk_busy is set during any ATA PIO transfer. The IRQ0 task-switcher
; checks it and refuses to switch tasks while it is non-zero — the ATA
; protocol can time out its DRQ window if we context-switch away
; mid-transfer, since the PIT would fire and the new task could call
; into a different ATA routine.
disk_busy        resb 1


; --- Block FS cache (used by fs_core.inc) ----------------------------------
; sb_buf is the in-memory copy of the superblock. The on-disk superblock
; is at fs_base_lba + 0; we read it at boot and write it back on every
; metadata change.
alignb 4
sb_buf           resb 512                 ; superblock cache
inode_buf        resb INODE_SIZE          ; current inode cache
inode_sector_buf resb 1024                ; raw 2-sector scratch for inode R/W
fs_bitmap        resb (FS_BITMAP_MAX_BLOCKS / 8)  ; block allocation bitmap

; dir_block_buf and file_block_buf are reused for both read-modify-write
; of existing data and as scratch for new allocations. Keeping them at
; full BLOCK_SIZE means we can do an entire 4 KB read into one of them,
; modify, and write back without a second ATA cycle.
dir_block_buf    resb BLOCK_SIZE
file_block_buf   resb BLOCK_SIZE
indirect_buf     resb BLOCK_SIZE          ; single-indirect pointer block

; Scratch variables used by dir_add_entry / file_read / file_write. These
; are reused across calls — there is no reentrancy here, ATA routines
; run with interrupts effectively disabled.
curr_block_idx   resd 1
file_offset      resd 1
file_bytes_left  resd 1
name_ptr         resd 1
child_inum       resd 1
bmap_slot        resd 1                   ; bmap_alloc: target pointer slot
bmap_data        resd 1                   ; bmap_alloc: allocated data block
file_type        resd 1


; --- Graphics engine state (used by graphics.inc) ---------------------------
; gfx_mode is 0 in text mode 03h, 1 in graphics Mode 13h. font_save holds a
; snapshot of the BIOS 8x16 text font captured at boot (gfx_save_font); it is
; restored on gfx_exit and used as the glyph source for gfx_char/gfx_string.
; gfx_backbuf is the 64000-byte off-screen buffer all primitives draw into;
; gfx_blit copies it to the visible framebuffer at 0xC00A0000.
alignb 4
gfx_mode         resb 1                   ; 0 = text, 1 = graphics
font_saved       resb 1                   ; 1 once font_save is populated

; fillrect scratch
alignb 4
fr_x             resd 1
fr_y             resd 1
fr_w             resd 1
fr_h             resd 1
fr_color         resd 1
fr_yend          resd 1

; char / string scratch
gfx_cx           resd 1
gfx_cy           resd 1
gfx_ccolor       resd 1
gfx_glyph        resd 1
gfx_sx           resd 1
gfx_sy           resd 1
gfx_scolor       resd 1

; line (Bresenham) scratch
gfx_lx0          resd 1
gfx_ly0          resd 1
gfx_lx1          resd 1
gfx_ly1          resd 1
gfx_lcolor       resd 1
gfx_ldx          resd 1
gfx_ldy          resd 1
gfx_lsx          resd 1
gfx_lsy          resd 1
gfx_lerr         resd 1

; PS/2 mouse state (used by graphics.inc irq12 / mouse_init)
alignb 4
mouse_x          resd 1
mouse_y          resd 1
mouse_buttons    resd 1                   ; bit0 left, bit1 right, bit2 middle
mouse_cycle      resd 1                   ; packet byte index 0..2
mouse_pkt0       resb 1                   ; flags byte
mouse_pkt1       resb 1                   ; X delta
mouse_pkt2       resb 1                   ; Y delta

alignb 16
font_save        resb FONT_BYTES          ; 4096-byte 8x16 font snapshot
gfx_backbuf      resb GFX_FB_SIZE         ; 64000-byte off-screen buffer


; --- IDT (used by idt.inc) --------------------------------------------------
; 256 entries x 8 bytes each. Must be 8-byte aligned for the lidt operand.
alignb 8
idt_start:
    resb IDT_ENTRIES * 8
idt_end:


; --- Task stacks (used by task.inc) -----------------------------------------
; One 4 KB stack per task. They are 16-byte aligned to keep SSE-friendly
; alignment (we don't use SSE, but the cost is zero and the alignment
; helps debugging in case a stack-walk tool ever looks at these).
alignb 16
task0_stack:
    resb TASK_STACK_SIZE
task0_stack_top:

alignb 16
task1_stack:
    resb TASK_STACK_SIZE
task1_stack_top:

alignb 16
task2_stack:
    resb TASK_STACK_SIZE
task2_stack_top:


; --- Main kernel stack -------------------------------------------------------
; Used by the early boot code and by all ISRs (syscall_isr, irq0, irq1)
; when no task switch occurs. 16 KB is generous; the deepest call chain
; is syscall -> FS read -> ATA -> ~10 frames, well under 4 KB.
alignb 16
stack_bottom:
    resb 16384
stack_top:


; --- Page tables (used by paging.inc) ---------------------------------------
; All page tables are 4 KB and 4 KB-aligned because the x86 MMU requires
; the physical address of a page table to be page-aligned when written
; to a PDE or PTE.
alignb 4096
page_directory:
    resd PAGE_DIR_ENTRIES                ; 1024 PDEs (covers 4 GB)

alignb 4096
identity_page_table:
    resd PAGE_TABLE_ENTRIES              ; 1024 PTEs for 0x00000000..0x003FFFFF

alignb 4096
kernel_low_page_table:
    resd PAGE_TABLE_ENTRIES              ; 1024 PTEs for 0x00400000..0x007FFFFF

alignb 4096
heap_page_table_0:
    resd PAGE_TABLE_ENTRIES              ; 1024 PTEs for 0x00800000..0x00BFFFFF

alignb 4096
heap_page_table_1:
    resd PAGE_TABLE_ENTRIES              ; 1024 PTEs for 0x00C00000..0x00FFFFFF

alignb 4096
heap_page_table_2:
    resd PAGE_TABLE_ENTRIES              ; 1024 PTEs for 0x01000000..0x013FFFFF


; --- Heap range (used by memory.inc) -----------------------------------------
; heap_start and heap_end are compile-time constants (see constants.inc):
; absolute virtual addresses inside the window backed by the three
; heap_page_table_* tables above. There is NO .bss reservation for the
; heap itself — the frames are mapped on demand by alloc_page/map_page.


; --- Page bitmap (used by memory.inc) ---------------------------------------
; 1 bit per 4 KB page. 32 KB = 8192 dwords = 262144 pages = 1 GB
; addressable. Bit N tracks physical frame N * 4096.
; We allocate more than we need; the kernel only manages the first
; ~16 MB of physical RAM, but the bitmap is cheap and over-sizing it
; means we don't have to recompute its size when we add more memory.
alignb 4
page_bitmap:
    resb 32768


; --- Allocation table (used by memory.inc / exec.inc / syscall.inc) ---------
; Fixed-size table of (base_vaddr, page_count) pairs. Used by find_free_virt
; to know which virtual ranges are occupied, and by exec_bin to find the
; range to free when a userland program returns. 128 entries is enough
; for the current load (1-3 active allocations in normal use).
alloc_table_count equ 128
alloc_table:
    resd alloc_table_count * 2
