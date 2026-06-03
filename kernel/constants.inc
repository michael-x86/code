; =============================================================================
; constants.inc — shared equates, structures, and macros
; =============================================================================

; --- GDT segment selectors ---------------------------------------------------
CODE_SEG equ 0x08
DATA_SEG equ 0x10

; --- Kernel load address (physical) ------------------------------------------
KERNEL_VIRTUAL_BASE  equ 0xC0100000

; --- Paging ------------------------------------------------------------------
PAGE_SIZE            equ 4096
PAGE_DIR_ENTRIES     equ 1024
PAGE_TABLE_ENTRIES   equ 1024
PAGE_PRESENT_RW      equ 3       ; present + writable

; --- Physical memory layout (all in MB) --------------------------------------
KERNEL_LOW_PHY       equ 0x00400000   ; 4 MB — kernel code/data
HEAP_REGION_0_PHY    equ 0x00800000   ; 8 MB
HEAP_REGION_1_PHY    equ 0x00C00000   ; 12 MB
HEAP_REGION_2_PHY    equ 0x01000000   ; 16 MB
HEAP_VIRTUAL_START   equ 0xC0000000
HEAP_TOTAL_SIZE      equ 12 * 1024 * 1204   ; 12 MB

; --- VGA ----------------------------------------------------------------------
VGA_TEXT_BUFFER      equ 0xC00B8000
VGA_ROWS             equ 25
VGA_COLS             equ 80
VGA_COLOR_GREEN      equ 0x02

; --- PIT ----------------------------------------------------------------------
PIT_FREQUENCY        equ 100          ; Hz
PIT_DIVISOR          equ 11932        ; 1193182 / 100

; --- ATA PIO ----------------------------------------------------------------
; The kernel does not hardcode a single controller. Instead it reads
; fs_ata_base (port) and fs_ata_drive (select byte) from the bootloader
; handoff area at [0x500] and [0x504] before issuing any disk command.
ATA_CMD_READ         equ 0x20
ATA_CMD_WRITE        equ 0x30
ATA_CMD_CACHE_FLUSH  equ 0xE7
ATA_DRIVE_MASTER     equ 0xE0
ATA_DRIVE_SLAVE      equ 0xF0
ATA_DEV_CONTROL      equ 0x376
ATA_CMD_IDENTIFY     equ 0xEC

; --- PIC ----------------------------------------------------------------------
PIC_MASTER_CMD       equ 0x20
PIC_MASTER_DATA      equ 0x21
PIC_SLAVE_CMD        equ 0xA0
PIC_SLAVE_DATA       equ 0xA1
PIC_EOI              equ 0x20
PIC_INIT             equ 0x11
PIC_MASTER_OFFSET    equ 0x20
PIC_SLAVE_OFFSET     equ 0x28

; --- FS persistence (legacy — kept for build script compat) -------------------
FS_BASE_LBA          equ 512
SECTORS_PER_SLOT     equ 9
FS_PERSIST_BUF_SIZE  equ 4608          ; 512 + 4096

; --- New block filesystem layout ----------------------------------------------
; On-disk layout (relative to FS region start):
;   sector 0 = superblock (512 bytes)
;   sector 1+ = inode table (INODE_COUNT inodes, INODE_SIZE bytes each, rounded up)
;   then  = block bitmap (1 bit per data block, rounded up to sector)
;   then  = data blocks (BLOCK_SIZE bytes each)
; The FS region starts at FS_BASE_LBA on the disk (set at boot from kernel size).
BLOCK_SIZE           equ 4096
BLOCK_SECTORS        equ 8              ; 4096 / 512
INODE_SIZE           equ 128
INODE_COUNT          equ 256
INODE_TABLE_LBA      equ 2
; Superblock magic
FS_MAGIC             equ 0xEF53
; Inode types
INODE_FREE           equ 0
INODE_FILE           equ 1
INODE_DIR            equ 2
; Inode structure offsets within 128-byte inode:
INOFS_TYPE           equ 0    ; dword: 0=free 1=file 2=dir
INOFS_SIZE           equ 4    ; dword: file size in bytes
INOFS_FLAGS          equ 8    ; dword: reserved
INOFS_DIRECT         equ 12   ; 12 x dword direct block pointers (48 bytes)
INOFS_SIND           equ 60   ; dword: single-indirect block pointer
INOFS_DIND           equ 64   ; dword: double-indirect block pointer
INOFS_RESERVED       equ 68   ; 60 bytes reserved, total = 128
; Superblock structure offsets (within 512-byte sector):
SB_MAGIC             equ 0    ; dword: FS_MAGIC
SB_BLOCK_SIZE        equ 4    ; dword
SB_TOTAL_BLOCKS      equ 8    ; dword: total data blocks on disk
SB_INODE_COUNT       equ 12   ; dword
SB_FREE_BLOCKS       equ 16   ; dword
SB_FREE_INODES       equ 20   ; dword
SB_INODE_TABLE_LBA   equ 24   ; dword
SB_BITMAP_LBA        equ 28   ; dword
SB_DATA_LBA          equ 32   ; dword
SB_MOUNT_COUNT       equ 36   ; dword
SB_MAX_BLOCKS        equ 40   ; dword: absolute capacity
; Directory entry structure (variable-length, stored in data blocks):
;   dword  inode_number
;   word   rec_len (total record length including padding)
;   byte   name_len
;   byte   file_type (1=file 2=dir)
;   char[] name (name_len bytes, no null required but null-terminated)
DIRENT_NAME_MAX      equ 48
; FS bitmap buffer in BSS (tracks free/used data blocks)
FS_BITMAP_MAX_BLOCKS equ 4096  ; max blocks we can track (4096 * 4096 = 16 MB)

; --- Shell --------------------------------------------------------------------
CMD_BUF_SIZE         equ 64
HIST_COUNT_MAX       equ 32
HIST_RECORD_SIZE     equ 64
ARGV_MAX             equ 16
KBD_BUF_SIZE         equ 256
CWD_BUF_SIZE         equ 128

; --- Tasks --------------------------------------------------------------------
TASK_COUNT           equ 3
TASK_STACK_SIZE      equ 4096

; --- IDT ----------------------------------------------------------------------
IDT_ENTRIES          equ 256
IDT_PRESENT          equ 10001110b
IDT_DPL3             equ 11101111b    ; syscall trap gate: DPL=3, preserves IF

; --- Syscall numbers ----------------------------------------------------------
%assign SYSCALL_PUTCHAR     0
%assign SYSCALL_PRINT       1
%assign SYSCALL_PRINT_CR    2
%assign SYSCALL_NEWLINE     3
%assign SYSCALL_CLS         4
%assign SYSCALL_PRINT_HEX   5
%assign SYSCALL_PRINT_INT   6
%assign SYSCALL_GET_KEY     7
%assign SYSCALL_GET_TICK    8
%assign SYSCALL_SHUTDOWN    9
%assign SYSCALL_READ_MEM    10
%assign SYSCALL_GETCWD      11
%assign SYSCALL_CHDIR       12
%assign SYSCALL_LIST_DIR    13
%assign SYSCALL_GET_ARG     14
%assign SYSCALL_STAT        15
%assign SYSCALL_PRINT_N     16
%assign SYSCALL_CREATE      17
%assign SYSCALL_WRITE       18
%assign SYSCALL_UNLINK      19
%assign SYSCALL_MKDIR       20
%assign SYSCALL_RMDIR       21
%assign SYSCALL_GET_PS_INFO 22
%assign SYSCALL_ALLOC_PAGES  23
%assign SYSCALL_FREE_PAGES   24
%assign SYSCALL_WRITE_MEM    25
%assign SYSCALL_HEX2INT      26
%assign SYSCALL_ASC2INT      27
%assign SYSCALL_GET_PROC_INFO 28
%assign SYSCALL_ITOA          29
%assign SYSCALL_GET_CONFIG    30
%assign SYSCALL_STACK_DUMP    31
%assign SYSCALL_ALLOC         32
%assign SYSCALL_DEALLOC       33
%assign SYSCALL_PEEK          34
%assign SYSCALL_POKE          35
SYSCALL_COUNT        equ 36

; --- FS entry structure offsets -----------------------------------------------
FS_NAME_LEN          equ 56
FS_REC_SIZE          equ 68
FS_OFS_TYPE          equ 56
FS_OFS_DATA          equ 60
FS_OFS_SIZE          equ 64

; --- FS entry types -----------------------------------------------------------
FS_TYPE_FREE         equ 0
FS_TYPE_DIR          equ 1
FS_TYPE_FILE         equ 2
FS_TYPE_EXEC         equ 2

; --- Macro: print a null-terminated string address ---------------------------
%macro PRINT_STRING 1
    push esi
    mov esi, %1
    call print
    pop esi
%endmacro

%macro PRINT_STRING_CR 1
    push esi
    mov esi, %1
    call print_cr
    pop esi
%endmacro
