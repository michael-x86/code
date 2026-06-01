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
