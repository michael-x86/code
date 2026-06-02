; =============================================================================
; bss.inc — zero-initialized data (not emitted in binary)
; =============================================================================

section .bss

; --- Tab completion state ---
alignb 16
tab_match_count  resd 1
tab_single_ptr   resd 1

; --- Task state ---
task0_esp        resd 1
task1_esp        resd 1
task2_esp        resd 1
current_task     resd 1

; --- Buffers ---
alignb 4
kbd_head         resd 1
kbd_tail         resd 1
kbd_buf          resb KBD_BUF_SIZE
cmd_buf          resb CMD_BUF_SIZE
hist_buf         resb HIST_COUNT_MAX * HIST_RECORD_SIZE
dir_buf          resb 512

; --- VFS state ---
cwd_buf          resb CWD_BUF_SIZE
resolve_buf      resb CWD_BUF_SIZE
path_buf         resb CWD_BUF_SIZE
tmp_dst          resd 1
tmp_left         resd 1
persist_buf      resb FS_PERSIST_BUF_SIZE

; --- Block filesystem state ---
alignb 4
fs_base_lba      resd 1                ; disk LBA where FS region begins
fs_ready         resb 1                ; 1 once superblock has been loaded
disk_busy        resb 1                ; set during ATA PIO to defer task switch
sb_buf           resb 512              ; superblock cache
inode_buf        resb INODE_SIZE       ; current inode cache
inode_sector_buf resb 1024             ; raw sector(s) for inode R/W
fs_bitmap        resb (FS_BITMAP_MAX_BLOCKS / 8)  ; block bitmap cache
dir_block_buf    resb BLOCK_SIZE       ; directory data block cache
file_block_buf   resb BLOCK_SIZE       ; file data block cache
indirect_buf     resb BLOCK_SIZE       ; indirect block cache
curr_block_idx   resd 1                ; current block index helper
file_offset      resd 1                ; current file offset helper
file_bytes_left  resd 1                ; remaining bytes helper
name_ptr         resd 1                ; name pointer helper
child_inum       resd 1                ; child inode number helper
file_type        resd 1                ; file type helper

; --- IDT ---
alignb 8
idt_start:
    resb IDT_ENTRIES * 8
idt_end:

; --- Task stacks ---
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

; --- Kernel stack ---
alignb 16
stack_bottom:
    resb 16384
stack_top:

; --- Page tables (page-aligned) ---
alignb 4096
page_directory:
    resd PAGE_DIR_ENTRIES

alignb 4096
identity_page_table:
    resd PAGE_TABLE_ENTRIES

alignb 4096
kernel_low_page_table:
    resd PAGE_TABLE_ENTRIES

alignb 4096
heap_page_table_0:
    resd PAGE_TABLE_ENTRIES

alignb 4096
heap_page_table_1:
    resd PAGE_TABLE_ENTRIES

alignb 4096
heap_page_table_2:
    resd PAGE_TABLE_ENTRIES

; --- Heap and page bitmap ---
alignb 4
heap_start:
    resb 12 * 1024 * 1024
heap_end:

alignb 4
page_bitmap:
    resb 32768

; --- Allocation table ---
alloc_table_count equ 128
alloc_table:
    resd alloc_table_count * 2
