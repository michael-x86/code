; =============================================================================
; constants.inc — shared equates, structure offsets, syscall numbers
; =============================================================================
;
; This file defines everything that is purely a compile-time constant:
;   - CPU / segment selectors
;   - Paging geometry
;   - Physical / virtual memory layout
;   - VGA text-mode geometry
;   - PIT frequency
;   - ATA command codes and port offsets
;   - PIC ports and ICW/OCW values
;   - On-disk block-FS geometry (block size, inode count, magic)
;   - Structure offsets within the superblock, inode, and directory entry
;   - Shell buffer sizes
;   - Task count and stack size
;   - IDT gate-type bits
;   - The 36 syscall numbers
;
; Nothing in this file allocates storage or generates code. Including it
; in any module just adds the equates to the assembler's symbol table.
; =============================================================================


; --- GDT segment selectors ---------------------------------------------------
; The bootloader installs a flat 4 GB GDT with code at 0x08 and data at 0x10.
; Kernel code runs with CS=0x08, all data segments (DS/ES/SS/FS/GS) at 0x10.
CODE_SEG        equ 0x08
DATA_SEG        equ 0x10


; --- Kernel load address ------------------------------------------------------
; The bootloader copies the kernel from LBA 1..N to physical 0x00100000 and
; jumps to it. We assemble with [org 0xC0100000] so the kernel's virtual
; addresses start 3 GB into the address space. The lower 4 MB is
; identity-mapped so the same code/data can be reached via either address
; until paging is enabled at the end of the entry sequence.
KERNEL_VIRTUAL_BASE equ 0xC0100000


; --- Paging geometry ---------------------------------------------------------
; 4 KB pages, 1024 entries per table, 1024 entries per directory.
; A page table therefore covers 4 MB; a directory covers 4 GB.
PAGE_SIZE          equ 4096
PAGE_DIR_ENTRIES   equ 1024
PAGE_TABLE_ENTRIES equ 1024
; PTE flag bits: present (bit 0) + read/write (bit 1). All our mappings
; are kernel-mode RW; we do not yet use the NX or user-bit.
PAGE_PRESENT_RW    equ 3


; --- Physical memory layout (all in MB) --------------------------------------
; The first 4 MB is the BIOS / VGA / bootloader-handoff region. The kernel
; itself lives at 4 MB. The next 12 MB (split into 3 x 4 MB regions) form
; the page-allocator pool. We use it directly — no separate bitmap for the
; first 4 MB, since the kernel image and BIOS structures live there.
KERNEL_LOW_PHY    equ 0x00400000   ; kernel code/data
HEAP_REGION_0_PHY equ 0x00800000
HEAP_REGION_1_PHY equ 0x00C00000
HEAP_REGION_2_PHY equ 0x01000000

; The virtual mirror of the same layout, placed at 0xC0000000+ so kernel
; pointers can use 32-bit values instead of awkward 64-bit math.
HEAP_VIRTUAL_START equ 0xC0000000
HEAP_TOTAL_SIZE    equ 12 * 1024 * 1024   ; 12 MB of addressable pool

; The heap allocator hands out virtual addresses inside the window that
; the three heap page tables (PDEs 770..772) actually back: the mirror of
; the three 4 MB physical heap regions. This MUST stay inside that window
; or map_page will refuse the mapping (CF=1) and the copy into the page
; will fault. 0xC0800000 .. 0xC1400000 == PDE 770..772 exactly (12 MB).
heap_start         equ HEAP_VIRTUAL_START + HEAP_REGION_0_PHY   ; 0xC0800000
heap_end           equ heap_start + HEAP_TOTAL_SIZE             ; 0xC1400000

; (Note: the previous constants.inc had a typo "1204" here — fixed to 1024.)


; --- VGA text mode ------------------------------------------------------------
; The VGA text framebuffer is at physical 0xB8000. After paging is enabled
; we access it through the higher-half alias at 0xC00B8000.
VGA_TEXT_BUFFER    equ 0xC00B8000
VGA_ROWS           equ 25
VGA_COLS           equ 80
VGA_COLOR_GREEN    equ 0x02     ; green-on-black, our default shell color
VGA_COLOR_BANNER   equ 0x20     ; black-on-green, used for the top banner row

; --- Scrollback buffer --------------------------------------------------------
; Screen snapshots for Page Up/Down scrolling. Each snapshot is the full
; VGA text buffer (VGA_COLS * VGA_ROWS * 2 bytes). Up to 50 snapshots.
SCROLLBACK_MAX     equ 50
SCREEN_SIZE        equ VGA_COLS * VGA_ROWS * 2   ; 4000 bytes


; --- Graphics framebuffer -----------------------------------------------------
; QEMU/std VGA exposes a Bochs VBE dispi linear framebuffer. We use the
; 640x400x256 mode and map its LFB at a fixed higher-half alias.
VGA_APERTURE_PHYS  equ 0x000A0000
VGA_APERTURE_VIRT  equ 0xC00A0000

VBE_LFB_PHYS       equ 0xE0000000
VBE_LFB_VIRT       equ 0xC2000000
VBE_LFB_PDE        equ 776
VBE_DISPI_IOPORT_INDEX equ 0x01CE
VBE_DISPI_IOPORT_DATA  equ 0x01CF
VBE_DISPI_ID4      equ 0xB0C4
VBE_DISPI_ENABLED  equ 0x0001
VBE_DISPI_LFB_ENABLED equ 0x0040
VBE_DISPI_8BIT_DAC_ENABLED equ 0x0020
VBE_DISPI_INDEX_ID equ 0
VBE_DISPI_INDEX_XRES equ 1
VBE_DISPI_INDEX_YRES equ 2
VBE_DISPI_INDEX_BPP equ 3
VBE_DISPI_INDEX_ENABLE equ 4
VBE_DISPI_INDEX_VIRT_WIDTH equ 5
VBE_DISPI_INDEX_VIRT_HEIGHT equ 6
VBE_DISPI_INDEX_X_OFFSET equ 7
VBE_DISPI_INDEX_Y_OFFSET equ 8

GFX_FB_PHYS        equ VBE_LFB_PHYS
GFX_FB_VIRT        equ VBE_LFB_VIRT
GFX_WIDTH          equ 640
GFX_HEIGHT         equ 400
GFX_FB_SIZE        equ GFX_WIDTH * GFX_HEIGHT    ; 256000 bytes
FONT_WIDTH         equ 8
FONT_HEIGHT        equ 16
FONT_BYTES         equ 256 * FONT_HEIGHT          ; 4096-byte font snapshot


; --- PIT (Programmable Interval Timer) ---------------------------------------
; Channel 0 of the PIT drives IRQ0, which we use for preemptive scheduling.
; The base clock is ~1.193182 MHz; with divisor 11932 we get exactly 100 Hz.
PIT_FREQUENCY      equ 100
PIT_DIVISOR        equ 11932


; --- ACPI (Advanced Configuration and Power Interface) -------------------------
; ACPI tables live in physical memory 0xE0000-0xFFFFF (RSDP scan region).
; The RSDP signature is "RSD PTR " (8 bytes). RSDT/XSDT contain arrays of
; pointers to other description tables.
ACPI_RSDP_SCAN_START equ 0x000E0000
ACPI_RSDP_SCAN_END   equ 0x000FFFFF
ACPI_MAPPED_END      equ 0x01400000   ; first byte NOT covered by PDEs 0-4
ACPI_RSDP_SIGNATURE  equ "RSD PTR "
ACPI_SDT_SIGNATURE   equ "RSDT"        ; Root System Description Table

; FADT field offsets (from start of table, after 36-byte SDT header)
FADT_FACS           equ 36             ; dword: FACS physical addr
FADT_DSDT           equ 40             ; dword: DSDT physical addr
FADT_SMI_CMD        equ 48             ; byte: SMI command port
FADT_ACPI_ENABLE    equ 49             ; byte
FADT_ACPI_DISABLE   equ 50             ; byte
FADT_S4BIOS_REQ     equ 51             ; byte
FADT_PSTATE_CNT     equ 52             ; byte
FADT_PM1a_EVT_BLK   equ 56             ; dword: PM1a event block base port
FADT_PM1b_EVT_BLK   equ 60             ; dword
FADT_PM1a_CNT_BLK   equ 64             ; dword: PM1a control block base port
FADT_PM1b_CNT_BLK   equ 68             ; dword
FADT_PM2_CNT_BLK    equ 72             ; dword
FADT_PM_TMR_BLK     equ 76             ; dword
FADT_GPE0_BLK       equ 80             ; dword
FADT_GPE1_BLK       equ 84             ; dword
FADT_PM1_EVT_LEN    equ 88             ; byte
FADT_PM1_CNT_LEN    equ 89             ; byte
FADT_PM2_CNT_LEN    equ 90             ; byte
FADT_PM_TMR_LEN     equ 91             ; byte
FADT_GPE0_BLK_LEN   equ 92             ; byte
FADT_GPE1_BLK_LEN   equ 93             ; byte
FADT_GPE1_BASE      equ 94             ; byte
FADT_CST_CNT        equ 95             ; byte
FADT_C2_LATENCY     equ 96             ; word
FADT_C3_LATENCY     equ 98             ; word
FADT_FLUSH_SIZE     equ 100            ; word
FADT_FLUSH_STRIDE   equ 102            ; word
FADT_DUTY_OFFSET    equ 104            ; byte
FADT_DUTY_WIDTH     equ 105            ; byte
FADT_DAY_ALARM      equ 106            ; byte
FADT_MONTH_ALARM    equ 107            ; byte
FADT_CENTURY        equ 108            ; byte
FADT_IAPC_BOOT_ARCH equ 109            ; word
FADT_FLAGS          equ 112            ; dword
FADT_RESET_REG      equ 116            ; 12-byte Generic Address Structure
  FADT_RESET_REG_ASP equ 116           ; byte: AddressSpaceId (0=mem, 1=I/O)
  FADT_RESET_REG_BW  equ 117           ; byte: RegisterBitWidth
  FADT_RESET_REG_BO  equ 118           ; byte: RegisterBitOffset
  FADT_RESET_REG_ADDR equ 120          ; qword: Address (port or MMIO)
  FADT_RESET_REG_AS  equ 128           ; byte: AccessSize
FADT_RESET_VALUE    equ 129            ; byte

; ACPI sleep / power-off constants
; SCI_EN bit must be set when writing SLP_TYPx to PM1a_CNT
SCI_EN              equ 1              ; bit 0

; Default SLP_TYPx for S5 (soft-off) — matches QEMU's \_S5 package {0x05, 0x05, ...}
; In Phase 4 these will come from DSDT AML parsing.
ACPI_S5_SLP_TYPa    equ 5
ACPI_S5_SLP_TYPb    equ 5

; Typical FADT reset register values
; I/O port 0xCF9, value 0x06 (QEMU) or 0x0E (real hardware)
ACPI_RESET_VALUE    equ 0x06
ACPI_RESET_PORT     equ 0xCF9

; PM1a_CNT_BLK port offset for the SLP_TYPx field
PM1a_CNT_SLEEP      equ 0              ; SLP_TYPx goes in bits 0-4 of PM1a_CNT+0

; Keyboard controller port 0x64 reset (fallback)
KBD_RESET_PORT      equ 0x64
KBD_RESET_VALUE     equ 0xFE

; --- ATA PIO -----------------------------------------------------------------
; The kernel does not hardcode a single controller. The bootloader writes
; the active port base, drive-select byte, and FS base LBA to physical
; memory at 0x500, 0x504, 0x508 respectively. We read those at boot.
ATA_CMD_READ       equ 0x20
ATA_CMD_WRITE      equ 0x30
ATA_CMD_CACHE_FLUSH equ 0xE7
ATA_DRIVE_MASTER   equ 0xE0
ATA_DRIVE_SLAVE    equ 0xF0
ATA_DEV_CONTROL    equ 0x376
ATA_CMD_IDENTIFY   equ 0xEC


; --- PIC (Programmable Interrupt Controller) ---------------------------------
; 8259A master/slave pair. We remap IRQ0..7 to INT 0x20..0x27 and IRQ8..15
; to INT 0x28..0x2F to keep the low 32 vectors free for CPU exceptions.
PIC_MASTER_CMD     equ 0x20
PIC_MASTER_DATA    equ 0x21
PIC_SLAVE_CMD      equ 0xA0
PIC_SLAVE_DATA     equ 0xA1
PIC_EOI            equ 0x20      ; End-Of-Interrupt command
PIC_INIT           equ 0x11      ; ICW1: edge-triggered, cascade, ICW4 needed
PIC_MASTER_OFFSET  equ 0x20
PIC_SLAVE_OFFSET   equ 0x28


; --- FS persistence (legacy — kept for build script compat) -----------------
; These constants are referenced by the build pipeline and some legacy
; code paths. The new block-based filesystem does not use them.
FS_BASE_LBA        equ 512
SECTORS_PER_SLOT   equ 9
FS_PERSIST_BUF_SIZE equ 4608      ; 512 + 4096


; --- New block filesystem layout --------------------------------------------
;
; On-disk layout (relative to FS region start at fs_base_lba):
;   sector 0            superblock (512 bytes)
;   sector 1+           inode table, INODE_COUNT inodes, INODE_SIZE bytes each
;   then                block bitmap, 1 bit per data block
;   then                data blocks, BLOCK_SIZE bytes each
;
; The FS region starts at fs_base_lba on the disk. The bootloader stores
; that LBA at physical 0x508 before jumping to the kernel. All LBA
; constants in this file (INODE_TABLE_LBA, etc.) are FS-region-relative;
; convert to disk LBA by adding fs_base_lba.
BLOCK_SIZE         equ 4096
BLOCK_SECTORS      equ 8          ; 4096 / 512
INODE_SIZE         equ 128
INODE_COUNT        equ 256
INODE_TABLE_LBA    equ 2

; Superblock magic. 0xEF53 is the ext2/3/4 magic; reusing it makes
; the on-disk format faintly familiar to anyone who has poked at Linux.
FS_MAGIC           equ 0xEF53

; Inode type values (INOFS_TYPE field).
INODE_FREE         equ 0
INODE_FILE         equ 1
INODE_DIR          equ 2

; Block-mapping geometry. A file's first INODE_DIRECT_COUNT blocks are
; named by the direct pointers in the inode. Logical blocks beyond that
; are named through the single-indirect block, which holds PTRS_PER_BLOCK
; (BLOCK_SIZE/4) block numbers. MAX_FILE_BLOCKS is the largest file the
; current direct + single-indirect scheme can address.
;   max file size = 1036 blocks * 4096 = 4,243,456 bytes (~4.05 MB)
INODE_DIRECT_COUNT equ 12
PTRS_PER_BLOCK     equ BLOCK_SIZE / 4        ; 1024 dword pointers per block
MAX_FILE_BLOCKS    equ INODE_DIRECT_COUNT + PTRS_PER_BLOCK

; Inode structure offsets (within the 128-byte inode).
INOFS_TYPE         equ 0          ; dword: 0=free 1=file 2=dir
INOFS_SIZE         equ 4          ; dword: file size in bytes (0 for dirs)
INOFS_FLAGS        equ 8          ; dword: reserved
INOFS_DIRECT       equ 12         ; 12 dwords: direct block pointers (48 B)
INOFS_SIND         equ 60         ; dword: single-indirect block pointer
INOFS_DIND         equ 64         ; dword: double-indirect block pointer
INOFS_RESERVED     equ 68         ; reserved area (68..125)
INOFS_CRC          equ 126        ; word: CRC-16 of inode bytes [0, INOFS_CRC)

; Superblock structure offsets (within the 512-byte sector).
SB_MAGIC           equ 0          ; dword: FS_MAGIC
SB_BLOCK_SIZE      equ 4          ; dword
SB_TOTAL_BLOCKS    equ 8          ; dword: total data blocks on disk
SB_INODE_COUNT     equ 12         ; dword
SB_FREE_BLOCKS     equ 16         ; dword
SB_FREE_INODES     equ 20         ; dword
SB_INODE_TABLE_LBA equ 24         ; dword (FS-relative)
SB_BITMAP_LBA      equ 28         ; dword (FS-relative)
SB_DATA_LBA        equ 32         ; dword (FS-relative)
SB_MOUNT_COUNT     equ 36         ; dword
SB_MAX_BLOCKS      equ 40         ; dword: absolute capacity

; Directory entry layout (variable-length, packed into data blocks):
;   dword  inode_number
;   word   rec_len       (total record length including name, 4-byte aligned)
;   byte   name_len
;   byte   file_type     (1=file 2=dir or executable — distinguished by inode)
;   char[] name          (name_len bytes, NUL-terminated in practice)
DIRENT_NAME_MAX    equ 48

; FS bitmap buffer size. 4096 bits = 4096 blocks x 4 KB = 16 MB addressable
; by the bitmap. We size it generously even though gen_fs.py only needs ~64
; blocks for the current 14-command set; 4 KB of BSS is cheaper than
; computing the exact count and risking a buffer overflow.
FS_BITMAP_MAX_BLOCKS equ 4096


; --- Shell --------------------------------------------------------------------
; Sizes for the command-line editor, history ring, and keyboard buffer.
; All are deliberately small — the kernel BSS is small, and most
; command lines are well under 64 bytes.
CMD_BUF_SIZE       equ 64
HIST_COUNT_MAX     equ 32
HIST_RECORD_SIZE   equ 64
ARGV_MAX           equ 16
KBD_BUF_SIZE       equ 256
CWD_BUF_SIZE       equ 128


; --- Tasks --------------------------------------------------------------------
; Three round-robin tasks scheduled by IRQ0: two idle, one shell.
; Stack size of 4 KB per task is enough for a small call graph. The kernel
; runs shell + tasks in a single address space; no MMU context switch.
TASK_COUNT         equ 1
TASK_STACK_SIZE    equ 4096


; --- IDT ----------------------------------------------------------------------
IDT_ENTRIES        equ 256
; Gate-type byte for an interrupt gate: present, DPL=0, 32-bit, interrupt.
; Bit layout: P=1 DPL=00 0 D=1 1 1 0 = 0b10001110 = 0x8E.
IDT_PRESENT        equ 10001110b
; Trap-gate variant with DPL=3, used for the syscall entry (INT 0x80).
; Lets ring-3 code trigger the syscall. 0xEF.
IDT_DPL3           equ 11101111b


; --- Syscall numbers ---------------------------------------------------------
; Numbered by slot. Adding a new syscall: pick the next slot, implement the
; handler in syscall.inc, then add a matching entry to syscall_table in
; data.inc. The bounds check in syscall_isr rejects eax >= SYSCALL_COUNT.
%assign SYSCALL_PUTCHAR       0
%assign SYSCALL_PRINT         1
%assign SYSCALL_PRINT_CR      2
%assign SYSCALL_NEWLINE       3
%assign SYSCALL_CLS           4
%assign SYSCALL_PRINT_HEX     5
%assign SYSCALL_PRINT_INT     6
%assign SYSCALL_GET_KEY       7
%assign SYSCALL_GET_TICK      8
%assign SYSCALL_SHUTDOWN      9
%assign SYSCALL_READ_MEM      10
%assign SYSCALL_GETCWD        11
%assign SYSCALL_CHDIR         12
%assign SYSCALL_LIST_DIR      13
%assign SYSCALL_GET_ARG       14
%assign SYSCALL_STAT          15
%assign SYSCALL_PRINT_N       16
%assign SYSCALL_CREATE        17
%assign SYSCALL_WRITE         18
%assign SYSCALL_UNLINK        19
%assign SYSCALL_MKDIR         20
%assign SYSCALL_RMDIR         21
%assign SYSCALL_GET_PS_INFO   22
%assign SYSCALL_ALLOC_PAGES   23
%assign SYSCALL_FREE_PAGES    24
%assign SYSCALL_WRITE_MEM     25
%assign SYSCALL_HEX2INT       26
%assign SYSCALL_ASC2INT       27
%assign SYSCALL_GET_PROC_INFO 28
%assign SYSCALL_ITOA          29
%assign SYSCALL_GET_CONFIG    30
%assign SYSCALL_STACK_DUMP    31
%assign SYSCALL_ALLOC         32
%assign SYSCALL_DEALLOC       33
%assign SYSCALL_PEEK          34
%assign SYSCALL_POKE          35
%assign SYSCALL_GFX_ENTER     36
%assign SYSCALL_GFX_EXIT      37
%assign SYSCALL_GFX_CLEAR     38
%assign SYSCALL_GFX_PIXEL     39
%assign SYSCALL_GFX_FILLRECT  40
%assign SYSCALL_GFX_RECT      41
%assign SYSCALL_GFX_LINE      42
%assign SYSCALL_GFX_CHAR      43
%assign SYSCALL_GFX_STRING    44
%assign SYSCALL_GFX_BLIT      45
%assign SYSCALL_GFX_INFO      46
%assign SYSCALL_MOUSE         47
SYSCALL_COUNT      equ 49
; --- Legacy FS entry structure (still referenced by build pipeline) ---------
; Old 68-byte fixed-size file record. The current build does not embed
; this in the kernel image, but constants are kept here so any leftover
; code compiles without surprise.
FS_NAME_LEN        equ 56
FS_REC_SIZE        equ 68
FS_OFS_TYPE        equ 56
FS_OFS_DATA        equ 60
FS_OFS_SIZE        equ 64

FS_TYPE_FREE       equ 0
FS_TYPE_DIR        equ 1
FS_TYPE_FILE       equ 2
FS_TYPE_EXEC       equ 2     ; same numeric value as FS_TYPE_FILE — see vfs.inc


; --- Macros ------------------------------------------------------------------
;
; PRINT_STRING / PRINT_STRING_CR push esi before the call because the print
; routines do not preserve it (it is the conventional "string pointer" arg).
; If a caller already has esi in use, the push/pop is required to keep the
; caller's pointer alive across the print.
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
