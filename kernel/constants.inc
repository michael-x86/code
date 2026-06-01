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

; --- ATA PIO ------------------------------------------------------------------
ATA_DATA_PORT        equ 0x1F0
ATA_SECTOR_COUNT     equ 0x1F2
ATA_LBA_LOW          equ 0x1F3
ATA_LBA_MID          equ 0x1F4
ATA_LBA_HIGH         equ 0x1F5
ATA_DRIVE_HEAD       equ 0x1F6
ATA_STATUS_CMD       equ 0x1F7
ATA_CMD_READ         equ 0x20
ATA_CMD_WRITE        equ 0x30
ATA_CMD_CACHE_FLUSH  equ 0xE7
ATA_DRIVE_MASTER     equ 0xE0

; --- PIC ----------------------------------------------------------------------
PIC_MASTER_CMD       equ 0x20
PIC_MASTER_DATA      equ 0x21
PIC_SLAVE_CMD        equ 0xA0
PIC_SLAVE_DATA       equ 0xA1
PIC_EOI              equ 0x20
PIC_INIT             equ 0x11
PIC_MASTER_OFFSET    equ 0x20
PIC_SLAVE_OFFSET     equ 0x28

; --- FS persistence -----------------------------------------------------------
FS_BASE_LBA          equ 256
SECTORS_PER_SLOT     equ 3
FS_PERSIST_BUF_SIZE  equ 1536          ; 3 * 512

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
IDT_DPL3             equ 11101110b    ; syscall gate: DPL=3

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
SYSCALL_COUNT        equ 23

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
