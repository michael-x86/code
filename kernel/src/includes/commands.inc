; =============================================================================
; commands.inc — built-in shell commands and the cmd_table
; =============================================================================
;
; The dispatch table is a sequence of null-terminated name + 4-byte
; handler-address pairs, terminated by a single zero byte. The shell's
; dispatch_command walks this table in order; first match wins.
;
; Each command handler is a stand-alone subroutine that reads its
; arguments from argv[1..] (via the parser) and writes to stdout via
; putchar / print / newline. Strings used only by these commands
; (help text, error messages, register labels) live in this file's
; .rodata section to keep them co-located with the code that uses them.
;
; The non-command routines (show_regs, show_stack, print_hex_*, etc.)
; are also here — they're small enough that splitting them into
; another "diagnostics" module wouldn't pay for itself.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; peek_cmd — display 16 bytes starting at the magic address
;
; Useful for confirming the kernel is still running and the data
; segment is intact.
; -----------------------------------------------------------------------------
peek_cmd:
    mov esi, deadbeef
    push eax
    call newline
    mov ecx, 16
.loop:
    lodsb
    call print_hex_byte
    mov al, ' '
    call putchar
    loop .loop
    call newline
    call newline
    pop eax
    ret


; -----------------------------------------------------------------------------
; help_cmd — print the built-in command list
; -----------------------------------------------------------------------------
help_cmd:
    PRINT_STRING_CR help_me
    ret


; -----------------------------------------------------------------------------
; echo_cmd — print argv[1]
; -----------------------------------------------------------------------------
echo_cmd:
    mov eax, [argc]
    cmp eax, 2
    jl .done
    mov esi, [argv + 4]
.print:
    lodsb
    test al, al
    jz .done
    call putchar
    jmp .print
.done:
    call newline
    ret


; -----------------------------------------------------------------------------
; alloc_cmd — allocate pages from the heap, print the base vaddr
;   argv[1] = size in bytes (rounded up to next page)
; -----------------------------------------------------------------------------
alloc_cmd:
    mov ecx, [argc]
    cmp ecx, 2
    jne .usage
    mov esi, [argv + 4]
    call asc2int
    mov eax, edx
    test eax, eax
    jz .usage
    add eax, 4095
    shr eax, 12
    mov ecx, eax
    push ecx
    call find_free_virt
    pop ecx
    jc .failed
    mov edi, eax
    mov edx, eax
.alloc_loop:
    test ecx, ecx
    jz .done_alloc
    push edx
    push ecx
    call alloc_page
    pop ecx
    pop edx
    jc .failed
    mov ebx, eax
    mov eax, edx
    push edx
    push ecx
    mov ecx, PAGE_PRESENT_RW
    call map_page
    pop ecx
    pop edx
    jc .failed
    add edx, PAGE_SIZE
    dec ecx
    jmp .alloc_loop
.done_alloc:
    mov eax, edi
    mov ebx, edx
    sub ebx, edi
    shr ebx, 12
    call register_allocation
    call newline
    PRINT_STRING alloc_mem
    mov eax, edi
    call print_hex_dword
    call newline
    ret
.failed:
    PRINT_STRING_CR out_mem
    ret
.usage:
    PRINT_STRING_CR no_arg
    ret


; -----------------------------------------------------------------------------
; free_cmd — release a previous allocation by its base vaddr
;   argv[1] = base address (hex)
; -----------------------------------------------------------------------------
free_cmd:
    mov ecx, [argc]
    cmp ecx, 2
    jne .usage
    mov esi, [argv + 4]
    call hex2int
    mov esi, alloc_table
    mov ecx, alloc_table_count
.search:
    cmp [esi], eax
    je .found
    add esi, 8
    loop .search
    PRINT_STRING_CR bad_free_msg
    ret
.found:
    mov ebx, [esi + 4]
    push eax
    call free_pages
    pop eax
    mov dword [esi], 0
    mov dword [esi + 4], 0
    PRINT_STRING_CR free_ok_msg
    ret
.usage:
    PRINT_STRING_CR free_usage_msg
    ret


; -----------------------------------------------------------------------------
; heap_cmd — show current allocations and free bytes
; -----------------------------------------------------------------------------
heap_cmd:
    push eax
    push ecx
    push esi
    call newline
    mov esi, alloc_table
    mov ecx, alloc_table_count
.loop:
    mov eax, [esi]
    test eax, eax
    jz .next
    push eax
    mov al, '0'
    call putchar
    mov al, 'x'
    call putchar
    pop eax
    call print_hex_dword
    call newline
.next:
    add esi, 8
    dec ecx
    jnz .loop
    call calc_free_heap
    call print_int_decimal
    PRINT_STRING_CR in_bytes
    pop esi
    pop ecx
    pop eax
    ret


; -----------------------------------------------------------------------------
; sys_cmd — sanity test for the int 0x80 syscall interface
;
; Issues a newline, prints a known string, then sys_read_mem of the
; string pointer and prints the dword. If the kernel is wired up
; correctly, the first 4 bytes of sys_peek_msg should be the
; ASCII bytes 'p', 'e', 'e', 'k'.
; -----------------------------------------------------------------------------
sys_cmd:
    mov eax, 3                          ; sys_newline
    int 0x80
    mov esi, sys_peek_msg
    mov eax, 1                          ; sys_print
    int 0x80
    mov ebx, sys_peek_msg
    mov eax, 10                         ; sys_read_mem
    int 0x80
    mov ebx, eax
    mov eax, 5                          ; sys_print_hex
    int 0x80
    mov eax, 3                          ; sys_newline
    int 0x80
    ret


; -----------------------------------------------------------------------------
; ls_cmd — list the current working directory
;
; Repeatedly calls sys_list_dir with incrementing index until it
; returns -1. Appends '/' to directory entries for visual distinction.
; -----------------------------------------------------------------------------
ls_cmd:
    pushad
    xor ebx, ebx
.ls_loop:
    push ebx
    mov edi, dir_buf
    mov word [edi], 0
    call sys_list_dir
    pop ebx
    cmp eax, -1
    je .ls_done
    push ebx
    push eax
    mov esi, dir_buf
    call print
    pop eax
    cmp eax, 2
    jne .ls_nodir
    mov al, '/'
    call putchar
.ls_nodir:
    call newline
    pop ebx
    inc ebx
    jmp .ls_loop
.ls_done:
    popad
    ret


; -----------------------------------------------------------------------------
; cd_cmd — change directory, or cd to /root with no argument
; -----------------------------------------------------------------------------
cd_cmd:
    pushad
    mov eax, [argc]
    cmp eax, 2
    jl .cd_home
    mov esi, [argv + 4]
    jmp .cd_do
.cd_home:
    mov esi, .cd_home_path
.cd_do:
    call sys_chdir
    cmp eax, -1
    jne .cd_done
    mov esi, .cd_err_msg
    call print_cr
.cd_done:
    popad
    ret

.cd_home_path: db "/root", 0
.cd_err_msg:   db "cd: no such directory", 0


; -----------------------------------------------------------------------------
; show_regs — print the saved register set from a pushad frame
;
; The pushad layout on the stack is EDI, ESI, EBP, ESP_dummy, EBX,
; EDX, ECX, EAX (low to high). We index with the offsets implied by
; that order, plus 4 bytes for the saved return address pushed by
; `call show_regs` (the callee then does pushad, so the offsets shift
; by 4 more).
; -----------------------------------------------------------------------------
show_regs:
    pushad
    mov ebp, esp
    call newline
    PRINT_STRING eax_lbl
    mov eax, [ebp + 28]
    call print_hex_dword
    call newline
    PRINT_STRING ecx_lbl
    mov eax, [ebp + 24]
    call print_hex_dword
    call newline
    PRINT_STRING edx_lbl
    mov eax, [ebp + 20]
    call print_hex_dword
    call newline
    PRINT_STRING ebx_lbl
    mov eax, [ebp + 16]
    call print_hex_dword
    call newline
    PRINT_STRING esp_lbl
    mov eax, [ebp + 12]
    call print_hex_dword
    call newline
    PRINT_STRING ebp_lbl
    mov eax, [ebp + 8]
    call print_hex_dword
    call newline
    PRINT_STRING esi_lbl
    mov eax, [ebp + 4]
    call print_hex_dword
    call newline
    PRINT_STRING edi_lbl
    mov eax, [ebp + 0]
    call print_hex_dword
    call newline
    call newline
    popad
    ret


; -----------------------------------------------------------------------------
; show_stack — print the top 8 dwords on the current stack
; -----------------------------------------------------------------------------
show_stack:
    pushad
    call newline
    mov ecx, 8
    mov edi, esp
.loop:
    mov eax, [edi]
    call print_hex_dword
    call newline
    add edi, 4
    dec ecx
    jnz .loop
    popad
    call newline
    ret


; -----------------------------------------------------------------------------
; fsdump_cmd — dump superblock, inodes, and directory entries
;
; Output format:
;   1. Superblock section — every field from sb_buf, one per line.
;   2. Inode walk         — one line per allocated inode, with the
;                            type, size, and direct block pointers.
;                            For directories, each child entry is
;                            indented underneath.
;
; Uses ensure_fs_ready first so the superblock is loaded even if the
; shell has not yet executed any FS-touching command.
; -----------------------------------------------------------------------------
fsdump_cmd:
    pushad
    call ensure_fs_ready

    ; --- Section 1: Superblock ------------------------------------------
    mov esi, fsd_sb_hdr
    call print
    call newline

    mov esi, fsd_lbl_magic
    call print
    mov eax, [sb_buf + SB_MAGIC]
    call print_hex_dword
    call newline

    mov esi, fsd_lbl_bsz
    call print
    mov eax, [sb_buf + SB_BLOCK_SIZE]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_tot
    call print
    mov eax, [sb_buf + SB_TOTAL_BLOCKS]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_ino
    call print
    mov eax, [sb_buf + SB_INODE_COUNT]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_fb
    call print
    mov eax, [sb_buf + SB_FREE_BLOCKS]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_fi
    call print
    mov eax, [sb_buf + SB_FREE_INODES]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_ilba
    call print
    mov eax, [sb_buf + SB_INODE_TABLE_LBA]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_blba
    call print
    mov eax, [sb_buf + SB_BITMAP_LBA]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_dlba
    call print
    mov eax, [sb_buf + SB_DATA_LBA]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_mnt
    call print
    mov eax, [sb_buf + SB_MOUNT_COUNT]
    call print_int_decimal
    call newline

    mov esi, fsd_lbl_base
    call print
    mov eax, [fs_base_lba]
    call print_int_decimal
    call newline

    ; --- Section 2: Inode walk -----------------------------------------
    call newline
    mov esi, fsd_ino_hdr
    call print
    call newline

    xor ebp, ebp                       ; inode number
.ino_loop:
    cmp ebp, [sb_buf + SB_INODE_COUNT]
    jae .ino_done
    mov ebx, ebp
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FREE
    je .ino_next
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    jne .ino_not_empty_dir
    cmp dword [inode_buf + INOFS_SIZE], 0
    je .ino_next

.ino_not_empty_dir:
    mov eax, ebp
    call print_int_decimal
    mov al, ':'
    call putchar
    mov al, ' '
    call putchar

    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    je .ino_is_dir
    mov esi, fsd_type_file
    call print
    jmp .ino_type_done
.ino_is_dir:
    mov esi, fsd_type_dir
    call print
.ino_type_done:

    mov esi, fsd_lbl_size
    call print
    mov eax, [inode_buf + INOFS_SIZE]
    call print_int_decimal

    mov esi, fsd_lbl_blks
    call print
    xor ecx, ecx
.blk_loop:
    cmp ecx, 12
    jae .blk_done
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .blk_next
    push ecx
    call print_int_decimal
    mov al, ' '
    call putchar
    pop ecx
.blk_next:
    inc ecx
    jmp .blk_loop
.blk_done:
    call newline

    ; --- Directory entry dump (only for directories) ------------------
    cmp dword [inode_buf + INOFS_TYPE], INODE_DIR
    jne .ino_next
    push ebp
    mov ebx, ebp
    xor ecx, ecx
.dir_blk_loop:
    cmp ecx, 12
    jae .dir_done
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .dir_next_blk
    push ebx
    push ecx
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ecx
    pop ebx

    mov edi, dir_block_buf
    mov edx, BLOCK_SIZE
.dir_ent_loop:
    cmp edx, 8
    jb .dir_next_blk
    movzx eax, word [edi + 4]
    test eax, eax
    jz .dir_next_blk
    cmp eax, 8
    jb .dir_next_blk
    cmp eax, edx
    ja .dir_next_blk
    mov eax, [edi]
    cmp eax, [sb_buf + SB_INODE_COUNT]
    jae .dir_skip_ent
    push edx
    push edi
    push ecx
    push ebx
    mov al, ' '
    call putchar
    call putchar
    call putchar
    call putchar
    lea esi, [edi + 8]
.dir_name_out:
    lodsb
    test al, al
    jz .dir_name_done
    call putchar
    jmp .dir_name_out
.dir_name_done:
    mov al, ' '
    call putchar
    mov al, '-'
    call putchar
    mov al, '>'
    call putchar
    mov al, ' '
    call putchar
    mov eax, [edi]
    call print_int_decimal
    call newline
    pop ebx
    pop ecx
    pop edi
    pop edx
.dir_skip_ent:
    movzx eax, word [edi + 4]
    sub edx, eax
    add edi, eax
    jmp .dir_ent_loop
.dir_next_blk:
    inc ecx
    jmp .dir_blk_loop
.dir_done:
    pop ebp
.ino_next:
    inc ebp
    jmp .ino_loop
.ino_done:
    popad
    ret


; -----------------------------------------------------------------------------
; fsck_cmd — verify every inode's CRC and report corruption
;
; Walks the inode table, calling verify_inode on each. Free inodes carry
; no checksum and are skipped by verify_inode. Reports each corrupt inode
; number and a final tally, or "clean" if every inode verifies.
; -----------------------------------------------------------------------------
fsck_cmd:
    pushad
    call ensure_fs_ready
    mov esi, fsck_hdr
    call print
    call newline

    xor ebp, ebp                        ; inode number
    xor edx, edx                        ; corrupt counter
.loop:
    cmp ebp, [sb_buf + SB_INODE_COUNT]
    jae .done
    mov ebx, ebp
    call verify_inode                   ; al = 1 if corrupt
    test al, al
    jz .next
    inc edx
    push edx
    mov esi, fsck_bad
    call print
    mov eax, ebp
    call print_int_decimal
    call newline
    pop edx
.next:
    inc ebp
    jmp .loop
.done:
    test edx, edx
    jnz .summary
    mov esi, fsck_clean
    call print
    call newline
    popad
    ret
.summary:
    mov esi, fsck_total
    call print
    mov eax, edx
    call print_int_decimal
    mov esi, fsck_total_suffix
    call print
    call newline
    popad
    ret


section .rodata

; Magic bytes that peek_cmd reads from.
deadbeef     db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

; Help text. 0x0D is interpreted as a newline by print_cr.
help_me:
    db  13, "peek  -  display 16 bytes at magic address", 13
    db  "regs  -  dump CPU registers", 13
    db  "stack -  display top of stack", 13
    db  "alloc -  allocate 4KB pages", 13
    db  "free  -  release an allocation", 13
    db  "heap  -  show heap allocations", 13
    db  "clear -  clear screen", 13
    db  "sys   -  test int 0x80 syscall", 13
    db  "fsdump-  dump FS superblock, inodes, dirs", 13
    db  "ls    -  list directory entries", 13
    db  "cd    -  change directory", 13
    db  "exit  -  shutdown", 13, 13, 0

; Register-dump labels, used by show_regs.
eax_lbl db "EAX: ", 0
ebx_lbl db "EBX: ", 0
ecx_lbl db "ECX: ", 0
edx_lbl db "EDX: ", 0
esi_lbl db "ESI: ", 0
edi_lbl db "EDI: ", 0
ebp_lbl db "EBP: ", 0
esp_lbl db "ESP: ", 0

; alloc_cmd / free_cmd / heap_cmd messages.
out_mem        db "OUT OF MEMORY", 13, 0
alloc_mem      db "heap pointer  : 0x", 0
no_arg         db 13, "usage: alloc <size>", 13, 0
in_bytes       db " bytes free", 13, 0
free_usage_msg db 13, "usage: free <hexaddr>", 13, 0
free_ok_msg    db "memory released", 13, 0
bad_free_msg   db "invalid allocation", 13, 0

; sys_cmd message.
sys_peek_msg   db "peek = 0x", 0

; Page-fault messages (used by irq.inc).
pf_msg         db 13, "PAGE FAULT", 13, 0
pf_addr        db "ADDRESS: 0x", 0

; fsdump section headers and field labels.
fsd_sb_hdr    db "--- superblock ---", 0
fsd_ino_hdr   db 13, "--- inodes ---", 0
fsd_lbl_magic db "  magic:       ", 0
fsd_lbl_bsz   db "  block_size:  ", 0
fsd_lbl_tot   db "  total_blocks:", 0
fsd_lbl_ino   db "  inode_count: ", 0
fsd_lbl_fb    db "  free_blocks: ", 0
fsd_lbl_fi    db "  free_inodes: ", 0
fsd_lbl_ilba  db "  inode_lba:   ", 0
fsd_lbl_blba  db "  bitmap_lba:  ", 0
fsd_lbl_dlba  db "  data_lba:    ", 0
fsd_lbl_mnt   db "  mount_count: ", 0
fsd_lbl_base  db "  fs_base_lba: ", 0
fsd_lbl_size  db "  size:", 0
fsd_lbl_blks  db "  blocks:", 0
fsd_type_file db "file", 0
fsd_type_dir  db "dir ", 0

; fsck (inode CRC verification) messages.
fsck_hdr          db "fsck: verifying inode checksums...", 0
fsck_bad          db "  CORRUPT inode ", 0
fsck_clean        db "fsck: clean, no corruption detected", 0
fsck_total        db "fsck: ", 0
fsck_total_suffix db " corrupt inode(s) detected", 0


; -----------------------------------------------------------------------------
; cmd_table — null-terminated list of built-in commands
;
; Format: db "name", 0  /  dd handler_address  /  ...  /  db 0  (end)
; dispatch_command walks this and calls the first matching handler.
; Order matters: if "exit" came before "echo", "echo" would still
; match first because dispatch scans by name, but keep the table in
; the order the help text lists them for readability.
; -----------------------------------------------------------------------------
cmd_table:
    db  "peek",  0
    dd  peek_cmd
    db  "regs",  0
    dd  show_regs
    db  "stack", 0
    dd  show_stack
    db  "clear", 0
    dd  cls
    db  "quit",  0
    dd  shutdown
    db  "exit",  0
    dd  shutdown
    db  "help",  0
    dd  help_cmd
    db  "echo",  0
    dd  echo_cmd
    db  "alloc", 0
    dd  alloc_cmd
    db  "free",  0
    dd  free_cmd
    db  "heap",  0
    dd  heap_cmd
    db  "sys",   0
    dd  sys_cmd
    db  "fsdump", 0
    dd  fsdump_cmd
    db  "fsck", 0
    dd  fsck_cmd
    db  "ls",    0
    dd  ls_cmd
    db  "cd",    0
    dd  cd_cmd
    db  0
