; =============================================================================
; vfs.inc — block-based virtual filesystem: superblock, inodes, block bitmap,
;           directory entries, ATA persistence
; =============================================================================
;
; On-disk layout:
;   LBA 0       bootloader
;   LBA 1       superblock (512 bytes)
;   LBA 2..N    inode table (INODE_COUNT * INODE_SIZE bytes, sector-aligned)
;   LBA N+1..M  block bitmap (1 bit per data block)
;   LBA M+1..   data blocks (BLOCK_SIZE = 4096 bytes each)
;
; Inode (INODE_SIZE = 128 bytes):
;   +0    dword   type (0=free 1=file 2=dir)
;   +4    dword   size (bytes for files, 0 for dirs)
;   +8    dword   flags (reserved)
;   +12   12xdirect block pointers (48 bytes)
;   +60   dword   single-indirect block pointer
;   +64   dword   double-indirect block pointer
;   +68   60 bytes reserved
;
; Directory entries (variable-length, packed into data blocks):
;   dword  inode_number
;   word   rec_len
;   byte   name_len
;   byte   file_type
;   char[] name (name_len bytes)
;
; At boot we load the superblock and block bitmap into memory.
; All metadata changes are written back to disk immediately.
; =============================================================================

; ── String helpers ──────────────────────────────────────────────────────────

str_eq:
    push esi
    push edi
.lp:
    mov al, [esi]
    mov ah, [edi]
    cmp al, ah
    jne .ne
    test al, al
    jz .eq
    inc esi
    inc edi
    jmp .lp
.eq:
    pop edi
    pop esi
    xor eax, eax
    ret
.ne:
    pop edi
    pop esi
    mov eax, 1
    ret

str_len:
    push esi
    xor ecx, ecx
.lp:
    cmp byte [esi], 0
    je .done
    inc esi
    inc ecx
    jmp .lp
.done:
    pop esi
    ret

str_copy:
    push eax
    push esi
    push edi
.lp:
    lodsb
    stosb
    test al, al
    jnz .lp
    pop edi
    pop esi
    pop eax
    ret

; -----------------------------------------------------------------------------
; basename_if_child — check if path is a direct child of cwd
;   in:  esi = entry path, edi = cwd
;   out: eax = pointer to basename within entry path, or 0 if not a child
; -----------------------------------------------------------------------------
basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi + 1], 0
    je .cwd_root
.mp:
    mov al, [edi]
    test al, al
    jz .after_cwd
    mov bl, [esi]
    cmp al, bl
    jne .nope
    inc esi
    inc edi
    jmp .mp
.after_cwd:
    cmp byte [esi], '/'
    jne .nope
    inc esi
    cmp byte [esi], 0
    je .nope
    mov ebx, esi
    mov edx, esi
.scan:
    mov al, [edx]
    test al, al
    jz .yes
    cmp al, '/'
    je .nope
    inc edx
    jmp .scan
.yes:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret
.cwd_root:
    cmp byte [esi], '/'
    jne .nope
    cmp byte [esi + 1], 0
    je .nope
    mov ebx, esi
    inc ebx
    mov edx, ebx
.sr:
    mov al, [edx]
    test al, al
    jz .root_ok
    cmp al, '/'
    je .nope
    inc edx
    jmp .sr
.root_ok:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret
.nope:
    xor eax, eax
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

; ── Path resolution ────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; fs_resolve — convert a path to an absolute path
;   in:  esi = path (absolute, ".", "..", or relative), edi = dst buffer (128B)
;   out: dst contains the resolved absolute path
; -----------------------------------------------------------------------------
fs_resolve:
    push eax
    push ebx
    push esi
    push edi
    cmp byte [esi], '/'
    je .abs
    cmp byte [esi], '.'
    jne .relative
    cmp byte [esi + 1], 0
    je .dot
    cmp byte [esi + 1], '.'
    jne .relative
    cmp byte [esi + 2], 0
    jne .relative
    jmp .dotdot
.dot:
    mov esi, cwd_buf
.abs:
.cp_abs:
    lodsb
    stosb
    test al, al
    jnz .cp_abs
    jmp .done
.dotdot:
    mov ebx, edi
    mov esi, cwd_buf
.cp_dd:
    lodsb
    stosb
    test al, al
    jnz .cp_dd
    dec edi
.find:
    dec edi
    cmp edi, ebx
    jbe .at_root
    cmp byte [edi], '/'
    jne .find
    cmp edi, ebx
    je .at_root
    mov byte [edi], 0
    jmp .done
.at_root:
    mov edi, ebx
    mov byte [edi], '/'
    mov byte [edi + 1], 0
    jmp .done
.relative:
    mov ebx, esi
    mov esi, cwd_buf
    cmp byte [esi + 1], 0
    je .root_join
.cp_cwd:
    lodsb
    stosb
    test al, al
    jnz .cp_cwd
    mov byte [edi - 1], '/'
    jmp .append
.root_join:
    mov byte [edi], '/'
    inc edi
.append:
    mov esi, ebx
.cp_name:
    lodsb
    stosb
    test al, al
    jnz .cp_name
.done:
    pop edi
    pop esi
    pop ebx
    pop eax
    ret

; ── ATA PIO disk access ────────────────────────────────────────────────────

; ── ATA PIO disk access (port/drive from bootloader handoff) ──────────────
;
; The bootloader stores at physical 0x500:
;   [0x500]  dword  fs_ata_base   — ATA port base (e.g. 0x1F0 primary, 0x170 secondary)
;   [0x504]  byte   fs_ata_drive  — drive select byte (0xE0 master, 0xF0 slave)
;   [0x508]  dword  fs_base_lba   — LBA offset where FS region begins
;
; All port offsets below are relative to fs_ata_base.
%define ATA_OFF_DATA      0
%define ATA_OFF_SECT_CNT  2
%define ATA_OFF_LBA_LOW   3
%define ATA_OFF_LBA_MID   4
%define ATA_OFF_LBA_HIGH  5
%define ATA_OFF_DRV_HEAD  6
%define ATA_OFF_STATUS   7

ata_read:
    pushad
    mov byte [disk_busy], 1
    mov ebx, eax
    mov ebp, ecx
    mov edx, [fs_ata_base]
    mov [ata_port_base], edx

    ; Select drive (LBA mode)
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    ; Small delay
    mov ecx, 4
.sel_delay:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in al, dx
    dec ecx
    jnz .sel_delay

    ; Wait for BSY clear
    mov ecx, 100000
.wb:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in al, dx
    test al, 0x80
    jz .ready
    dec ecx
    jnz .wb
    jmp .timeout
.ready:
    test al, 0x01
    jnz .timeout
    ; Re-select drive
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al
    ; Sector count
    mov dx, [ata_port_base]
    add dx, ATA_OFF_SECT_CNT
    mov eax, ebp
    out dx, al
    ; LBA low
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_LOW
    mov eax, ebx
    out dx, al
    ; LBA mid
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_MID
    shr eax, 8
    out dx, al
    ; LBA high
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_HIGH
    shr eax, 8
    out dx, al
    ; Send READ command
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov al, ATA_CMD_READ
    out dx, al
    mov ecx, ebp
.nxs:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov esi, 100000
.wd:
    in al, dx
    test al, 0x80
    jz .rd_check
    dec esi
    jnz .wd
    jmp .timeout
.rd_check:
    test al, 0x01
    jnz .timeout
    test al, 0x08
    jnz .rd_xfer
    dec esi
    jnz .wd
    jmp .timeout
.rd_xfer:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DATA
    push ecx
    mov ecx, 256
    rep insw
    pop ecx
    dec ecx
    jnz .nxs
    mov byte [disk_busy], 0
    popad
    ret
.timeout:
    mov byte [disk_busy], 0
    popad
    mov eax, -1
    ret

ata_write:
    pushad
    mov byte [disk_busy], 1
    mov ebx, eax
    mov ebp, ecx
    mov edx, [fs_ata_base]
    mov [ata_port_base], edx

    ; Select drive
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    mov ecx, 4
.wr_sel_delay:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in al, dx
    dec ecx
    jnz .wr_sel_delay

    mov ecx, 100000
.wb:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in al, dx
    test al, 0x80
    jz .wr_ready
    dec ecx
    jnz .wb
    jmp .wr_timeout
.wr_ready:
    test al, 0x01
    jnz .wr_timeout
    ; Re-select drive
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al
    ; Sector count
    mov dx, [ata_port_base]
    add dx, ATA_OFF_SECT_CNT
    mov eax, ebp
    out dx, al
    ; LBA low
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_LOW
    mov eax, ebx
    out dx, al
    ; LBA mid
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_MID
    shr eax, 8
    out dx, al
    ; LBA high
    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_HIGH
    shr eax, 8
    out dx, al
    ; Send WRITE command
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov al, ATA_CMD_WRITE
    out dx, al
    mov ecx, ebp
.nxs:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov esi, 100000
.wd:
    in al, dx
    test al, 0x80
    jz .wr_check
    dec esi
    jnz .wd
    jmp .wr_timeout
.wr_check:
    test al, 0x01
    jnz .wr_timeout
    test al, 0x08
    jnz .wr_xfer
    dec esi
    jnz .wd
    jmp .wr_timeout
.wr_xfer:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DATA
    push ecx
    mov ecx, 256
    rep outsw
    pop ecx
    dec ecx
    jnz .nxs
    ; Cache flush
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov al, ATA_CMD_CACHE_FLUSH
    out dx, al
    mov ecx, 100000
.wf:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in al, dx
    test al, 0x80
    jz .flush_done
    dec ecx
    jnz .wf
    jmp .wr_timeout
.flush_done:
    mov byte [disk_busy], 0
    popad
    ret
.wr_timeout:
    mov byte [disk_busy], 0
    popad
    mov eax, -1
    ret

; ── Superblock ─────────────────────────────────────────────────────────────

; FS_BASE_LBA is set by load_fs_persist before any disk access.
; It represents the disk LBA where the filesystem region begins.
; All LBA constants in this file (INODE_TABLE_LBA, etc.) are relative
; to the FS region start, so we add FS_BASE_LBA for actual disk reads.

load_superblock:
    pushad
    mov eax, [fs_base_lba]   ; superblock is at sector 0 of FS region
    mov ecx, 1
    mov edi, sb_buf
    call ata_read
    popad
    ret

write_superblock:
    pushad
    mov eax, [fs_base_lba]
    mov ecx, 1
    mov esi, sb_buf
    call ata_write
    popad
    ret

; ── Block bitmap ───────────────────────────────────────────────────────────

load_bitmap:
    pushad
    mov eax, [sb_buf + SB_BITMAP_LBA]
    add eax, [fs_base_lba]       ; convert FS-relative LBA to disk LBA
    mov ecx, [sb_buf + SB_TOTAL_BLOCKS]
    add ecx, 7
    shr ecx, 3
    add ecx, 511
    shr ecx, 9
    mov edi, fs_bitmap
    call ata_read
    popad
    ret

write_bitmap:
    pushad
    mov eax, [sb_buf + SB_BITMAP_LBA]
    add eax, [fs_base_lba]
    mov ecx, [sb_buf + SB_TOTAL_BLOCKS]
    add ecx, 7
    shr ecx, 3
    add ecx, 511
    shr ecx, 9
    mov esi, fs_bitmap
    call ata_write
    popad
    ret

; -----------------------------------------------------------------------------
; balloc — allocate a free data block
;   out: eax = block number, or -1 if none free
; -----------------------------------------------------------------------------
balloc:
    push ebx
    push ecx
    push edx
    push edi
    mov ecx, [sb_buf + SB_TOTAL_BLOCKS]
    xor ebx, ebx
    mov edi, fs_bitmap
.bitmap_lp:
    cmp ebx, ecx
    jae .no_free
    mov eax, ebx
    shr eax, 3
    movzx edx, byte [edi + eax]
    mov eax, ebx
    and eax, 7
    bt edx, eax
    jnc .found
    inc ebx
    jmp .bitmap_lp
.found:
    mov eax, ebx
    shr eax, 3
    mov edx, 1
    push ecx
    mov ecx, ebx
    and ecx, 7
    shl dl, cl
    or [fs_bitmap + eax], dl
    pop ecx
    call write_bitmap
    dec dword [sb_buf + SB_FREE_BLOCKS]
    call write_superblock
    mov eax, ebx
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret
.no_free:
    mov eax, -1
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; bfree — free a data block
;   in: ebx = block number
; -----------------------------------------------------------------------------
bfree:
    push eax
    push ecx
    push edx
    push edi
    mov eax, ebx
    shr eax, 3
    mov edx, 1
    mov ecx, ebx
    and ecx, 7
    shl dl, cl
    not dl
    and [fs_bitmap + eax], dl
    call write_bitmap
    inc dword [sb_buf + SB_FREE_BLOCKS]
    call write_superblock
    pop edi
    pop edx
    pop ecx
    pop eax
    ret

; ── Inode table ────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; load_inode — read an inode from disk into inode_buf
;   in:  ebx = inode number (0-based)
; -----------------------------------------------------------------------------
load_inode:
    pushad
    mov eax, ebx
    imul eax, INODE_SIZE
    mov ecx, eax
    shr ecx, 9
    add ecx, INODE_TABLE_LBA
    add ecx, [fs_base_lba]       ; convert to disk LBA
    and eax, 511
    push eax
    mov eax, ecx
    mov ecx, 2
    mov edi, inode_sector_buf
    call ata_read
    pop eax
    lea esi, [inode_sector_buf + eax]
    mov edi, inode_buf
    mov ecx, INODE_SIZE
    cld
    rep movsb
    popad
    ret

; -----------------------------------------------------------------------------
; write_inode — write inode_buf back to disk
;   in:  ebx = inode number
; -----------------------------------------------------------------------------
write_inode:
    pushad
    mov eax, ebx
    imul eax, INODE_SIZE
    mov ecx, eax
    shr ecx, 9
    add ecx, INODE_TABLE_LBA
    add ecx, [fs_base_lba]       ; convert to disk LBA
    and eax, 511
    push eax
    mov eax, ecx
    mov ecx, 2
    mov edi, inode_sector_buf
    call ata_read
    pop eax
    mov esi, inode_buf
    lea edi, [inode_sector_buf + eax]
    mov ecx, INODE_SIZE
    cld
    rep movsb
    mov eax, ebx
    imul eax, INODE_SIZE
    shr eax, 9
    add eax, INODE_TABLE_LBA
    add eax, [fs_base_lba]
    mov ecx, 2
    mov esi, inode_sector_buf
    call ata_write
    popad
    ret

; -----------------------------------------------------------------------------
; ialloc — allocate a free inode
;   out: eax = inode number, or -1 if none free
; -----------------------------------------------------------------------------
ialloc:
    push ebx
    push ecx
    push edx
    mov ecx, [sb_buf + SB_INODE_COUNT]
    xor ebx, ebx
.scan:
    cmp ebx, ecx
    jae .no_free
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FREE
    je .found
    inc ebx
    jmp .scan
.found:
    mov dword [inode_buf + INOFS_TYPE], INODE_FILE
    mov dword [inode_buf + INOFS_SIZE], 0
    call write_inode
    dec dword [sb_buf + SB_FREE_INODES]
    call write_superblock
    mov eax, ebx
    pop edx
    pop ecx
    pop ebx
    ret
.no_free:
    mov eax, -1
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; ifree — free an inode and all its data blocks
;   in:  ebx = inode number
; -----------------------------------------------------------------------------
ifree:
    pushad
    call load_inode
    mov ecx, 12
    lea esi, [inode_buf + INOFS_DIRECT]
.free_direct:
    mov eax, [esi]
    test eax, eax
    jz .next_d
    push ecx
    push esi
    mov ebx, eax
    call bfree
    pop esi
    pop ecx
    mov dword [esi], 0
.next_d:
    add esi, 4
    dec ecx
    jnz .free_direct
    mov eax, [inode_buf + INOFS_SIND]
    test eax, eax
    jz .no_sind
    push eax
    mov ecx, 1
    mov edi, indirect_buf
    call ata_read
    pop eax
    mov ebx, eax
    call bfree
    mov dword [inode_buf + INOFS_SIND], 0
.no_sind:
    mov dword [inode_buf + INOFS_TYPE], INODE_FREE
    mov dword [inode_buf + INOFS_SIZE], 0
    call write_inode
    inc dword [sb_buf + SB_FREE_INODES]
    call write_superblock
    popad
    ret

; ── Directory operations ───────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; dir_lookup — look up a name within a directory inode
;   in:  ebx = parent inode number, esi = name to find
;   out: eax = child inode number, or -1 if not found
; -----------------------------------------------------------------------------
dir_lookup:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    call load_inode
    mov ebp, 0
.dblk:
    cmp ebp, 12
    jae .not_found
    mov eax, [inode_buf + INOFS_DIRECT + ebp * 4]
    test eax, eax
    jz .not_found
    push ebp
    push ebx
    mov ebx, eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]       ; convert to disk LBA
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ebx
    pop ebp
    mov edi, dir_block_buf
    mov ecx, BLOCK_SIZE
.scan_entry:
    cmp ecx, 8
    jb .next_block
    movzx eax, word [edi + 4]
    test eax, eax
    jz .next_block
    push ecx
    push edi
    push esi
    lea edi, [edi + 8]
    call str_eq
    pop esi
    pop edi
    pop ecx
    test eax, eax
    jz .found_it
    movzx eax, word [edi + 4]
    sub ecx, eax
    add edi, eax
    jmp .scan_entry
.found_it:
    mov eax, [edi]
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.next_block:
    inc ebp
    jmp .dblk
.not_found:
    mov eax, -1
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; dir_add_entry — add a directory entry to a parent directory
;   in:  ebx = parent inode number, esi = name, edx = child inode number
;         cl = file_type (1=file 2=dir)
;   out: eax = 0 on success, -1 on error
;   Clobbers: all GPRs
; -----------------------------------------------------------------------------
dir_add_entry:
    pushad
    ; Save parameters to memory (pushad order: eax,ecx,edx,ebx,esp,ebp,esi,edi)
    mov [child_inum], edx
    mov [file_type], ecx
    mov [name_ptr], esi
    ; Compute rec_len needed: 8 + name_len, aligned to 4
    push ebx
    call str_len              ; ecx = name_len (esi = name_ptr still)
    mov eax, ecx
    add eax, 8 + 3
    and eax, ~3
    mov ebp, eax              ; ebp = rec_len needed
    pop ebx
    ; Load parent inode
    call load_inode
    ; Try to find space in existing data blocks
    mov dword [curr_block_idx], 0
.dae_blk:
    mov ecx, [curr_block_idx]
    cmp ecx, 12
    jae .dae_alloc
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .dae_alloc_here
    ; Read data block
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
    ; Scan for trailing free space
    mov edi, dir_block_buf
    mov eax, BLOCK_SIZE
.dae_scan:
    cmp eax, ebp
    jb .dae_next_blk
    movzx ecx, word [edi + 4] ; rec_len
    test ecx, ecx
    jz .dae_use_here
    sub eax, ecx
    add edi, ecx
    jmp .dae_scan
.dae_use_here:
    cmp eax, ebp
    jge .dae_found
.dae_next_blk:
    inc dword [curr_block_idx]
    jmp .dae_blk
.dae_alloc_here:
    ; Allocate a new data block
    call balloc
    cmp eax, -1
    je .dae_err
    ; Store in inode
    mov edx, eax
    push ebx
    mov ecx, [curr_block_idx]
    call load_inode
    pop ebx
    mov [inode_buf + INOFS_DIRECT + ecx * 4], edx
    call write_inode
    ; Zero the block
    mov edi, dir_block_buf
    mov ecx, BLOCK_SIZE / 4
    xor eax, eax
    cld
    rep stosd
    mov edi, dir_block_buf
    jmp .dae_write
.dae_alloc:
    jmp .dae_err
.dae_found:
    ; edi = where to write, ebp = rec_len
.dae_write:
    ; Write inode number
    mov eax, [child_inum]
    mov [edi], eax
    ; Write rec_len
    mov word [edi + 4], bp
    ; Write name_len
    mov esi, [name_ptr]
    push edi
    xor ecx, ecx
.dae_nlen:
    cmp byte [esi + ecx], 0
    je .dae_nlen_done
    inc ecx
    jmp .dae_nlen
.dae_nlen_done:
    pop edi
    mov byte [edi + 6], cl   ; name_len
    ; Write file_type
    mov eax, [file_type]
    mov byte [edi + 7], al
    ; Copy name
    lea edi, [edi + 8]
    mov esi, [name_ptr]
    cld
    rep movsb
    ; Write the data block back to disk
    ; Compute which block this is from curr_block_idx
    mov ecx, [curr_block_idx]
    push ebx
    call load_inode
    pop ebx
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov esi, dir_block_buf
    call ata_write
    popad
    xor eax, eax
    ret
.dae_err:
    popad
    mov eax, -1
    ret

; ── File lookup by path ────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; fs_lookup_inode — resolve a path to an inode number
;   in:  esi = absolute null-terminated path
;   out: eax = inode number, or -1 if not found
; -----------------------------------------------------------------------------
fs_lookup_inode:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    cmp byte [esi], '/'
    jne .not_found
    cmp byte [esi + 1], 0
    je .root_found
    mov ebx, 0               ; current inode = root
    inc esi
.component:
    mov edi, path_buf
.cp_comp:
    mov al, [esi]
    test al, al
    jz .last_comp
    cmp al, '/'
    je .got_comp
    stosb
    inc esi
    jmp .cp_comp
.got_comp:
    mov byte [edi], 0
    inc esi
    push esi
    mov esi, path_buf
    call dir_lookup
    pop esi
    cmp eax, -1
    je .not_found
    mov ebx, eax
    jmp .component
.last_comp:
    mov byte [edi], 0
    cmp byte [path_buf], 0
    je .root_found
    push esi
    mov esi, path_buf
    call dir_lookup
    pop esi
    cmp eax, -1
    je .not_found
    mov ebx, eax
.root_found:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.not_found:
    mov eax, -1
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; ── File read ──────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; file_read — read data from a file inode
;   in:  ebx = inode number, edi = dest buffer, ecx = count, edx = offset
;   out: eax = bytes read, or -1 on error
; -----------------------------------------------------------------------------
file_read:
    pushad
    mov ebp, edi             ; ebp = dest
    mov [file_bytes_left], ecx
    mov [file_offset], edx
    call load_inode
    ; Clamp to file size
    mov eax, [inode_buf + INOFS_SIZE]
    sub eax, [file_offset]
    jle .read_done
    cmp [file_bytes_left], eax
    jle .read_loop
    mov [file_bytes_left], eax
.read_loop:
    cmp dword [file_bytes_left], 0
    je .read_done
    mov eax, [file_offset]
    shr eax, 12              ; block index
    cmp eax, 12
    jae .read_done
    ; Get block pointer from inode
    mov ecx, [inode_buf + INOFS_DIRECT + eax * 4]
    test ecx, ecx
    jz .read_done
    ; Read data block
    push ebx
    mov ebx, ecx
    shl ecx, 3
    add ecx, [sb_buf + SB_DATA_LBA]
    add ecx, [fs_base_lba]
    mov eax, ecx
    mov ecx, BLOCK_SECTORS
    mov edi, file_block_buf
    call ata_read
    pop ebx
    ; Compute bytes to copy
    mov eax, [file_offset]
    and eax, 0xFFF           ; offset in block
    mov ecx, BLOCK_SIZE
    sub ecx, eax             ; bytes available in block
    cmp [file_bytes_left], ecx
    jle .rd_copy
    mov ecx, [file_bytes_left]
.rd_copy:
    ; Copy from file_block_buf[offset_in_block] to dest
    push esi
    push edi
    lea esi, [file_block_buf + eax]
    mov edi, ebp
    ; ecx already has bytes to copy
    cld
    rep movsb
    pop edi
    pop esi
    add ebp, ecx
    add [file_offset], ecx
    sub [file_bytes_left], ecx
    jmp .read_loop
.read_done:
    mov eax, ebp
    sub eax, [esp + 28]      ; original edi from pushad
    mov [esp + 28], eax      ; store in eax slot for popad
    popad
    ret

; ── File write ─────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; file_write — write data to a file inode
;   in:  ebx = inode number, esi = src buffer, ecx = count, edx = offset
;   out: eax = bytes written, or -1 on error
; -----------------------------------------------------------------------------
file_write:
    pushad
    mov [file_bytes_left], ecx
    mov [file_offset], edx
    mov ebp, esi             ; ebp = source pointer
    call load_inode
.wr_loop:
    cmp dword [file_bytes_left], 0
    je .wr_done
    mov eax, [file_offset]
    shr eax, 12              ; block index
    cmp eax, 12
    jae .wr_done
    ; Check if block is allocated
    mov ecx, [inode_buf + INOFS_DIRECT + eax * 4]
    test ecx, ecx
    jnz .wr_block_ok
    ; Allocate a new block
    call balloc
    cmp eax, -1
    je .wr_err
    ; Store in inode
    mov ecx, [file_offset]
    shr ecx, 12
    push ebx
    call load_inode
    pop ebx
    mov [inode_buf + INOFS_DIRECT + ecx * 4], eax
    call write_inode
    ; Zero the new block
    push ebx
    mov edi, file_block_buf
    mov ecx, BLOCK_SIZE / 4
    xor eax, eax
    cld
    rep stosd
    pop ebx
    mov ecx, [inode_buf + INOFS_DIRECT + ecx * 4]
    ; Write zeroed block to disk
    push ebx
    mov ebx, ecx
    shl ecx, 3
    add ecx, [sb_buf + SB_DATA_LBA]
    add ecx, [fs_base_lba]
    mov eax, ecx
    mov ecx, BLOCK_SECTORS
    mov esi, file_block_buf
    call ata_write
    pop ebx
.wr_block_ok:
    ; Read existing block
    mov eax, [file_offset]
    shr eax, 12
    mov ecx, [inode_buf + INOFS_DIRECT + eax * 4]
    push ebx
    mov ebx, ecx
    shl ecx, 3
    add ecx, [sb_buf + SB_DATA_LBA]
    add ecx, [fs_base_lba]
    mov eax, ecx
    mov ecx, BLOCK_SECTORS
    mov edi, file_block_buf
    call ata_read
    pop ebx
    ; Compute bytes to copy
    mov eax, [file_offset]
    and eax, 0xFFF
    mov ecx, BLOCK_SIZE
    sub ecx, eax
    cmp [file_bytes_left], ecx
    jle .wr_copy
    mov ecx, [file_bytes_left]
.wr_copy:
    ; Copy from source into file_block_buf
    push esi
    push edi
    lea edi, [file_block_buf + eax]
    mov esi, ebp
    cld
    rep movsb
    pop edi
    pop esi
    ; Write block back
    push ebx
    push ecx
    mov eax, [file_offset]
    shr eax, 12
    mov ecx, [inode_buf + INOFS_DIRECT + eax * 4]
    mov ebx, ecx
    shl ecx, 3
    add ecx, [sb_buf + SB_DATA_LBA]
    add ecx, [fs_base_lba]
    mov eax, ecx
    mov ecx, BLOCK_SECTORS
    mov esi, file_block_buf
    call ata_write
    pop ecx
    pop ebx
    add ebp, ecx
    add [file_offset], ecx
    sub [file_bytes_left], ecx
    jmp .wr_loop
.wr_done:
    ; Update file size if extended
    mov eax, [file_offset]
    call load_inode
    cmp eax, [inode_buf + INOFS_SIZE]
    jle .wr_no_size
    mov [inode_buf + INOFS_SIZE], eax
    call write_inode
.wr_no_size:
    popad
    xor eax, eax
    ret
.wr_err:
    popad
    mov eax, -1
    ret

; ── Persistence (boot-time) ────────────────────────────────────────────────

load_fs_persist:
    pushad
    ; Read ATA port base, drive select, and FS base LBA from bootloader handoff
    ; at physical 0x500 (identity-mapped in the lower 4 MB):
    ;   [0x500]  dword  ATA port base (e.g. 0x1F0)
    ;   [0x504]  byte   drive select (0xE0 master, 0xF0 slave)
    ;   [0x508]  dword  FS base LBA
    mov eax, [0x500]
    mov [fs_ata_base], eax
    mov al, [0x504]
    mov [fs_ata_drive], al
    mov eax, [0x508]
    mov [fs_base_lba], eax
    call load_superblock

    ; If the superblock is invalid, skip bitmap load to avoid long ATA waits
    ; based on garbage metadata.
    cmp dword [sb_buf + SB_MAGIC], FS_MAGIC
    jne .done
    mov eax, [sb_buf + SB_TOTAL_BLOCKS]
    test eax, eax
    jz .done
    cmp eax, FS_BITMAP_MAX_BLOCKS
    ja .done

    call load_bitmap
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; ensure_fs_ready — lightweight one-time FS init for command execution
;   Loads fs_base_lba and superblock only. Does not load bitmap.
; -----------------------------------------------------------------------------
ensure_fs_ready:
    pushad
    cmp byte [fs_ready], 1
    je .ok

    mov eax, [0x500]
    mov [fs_ata_base], eax
    mov al, [0x504]
    mov [fs_ata_drive], al
    mov eax, [0x508]
    mov [fs_base_lba], eax
    call load_superblock
    cmp dword [sb_buf + SB_MAGIC], FS_MAGIC
    jne .ok
    mov byte [fs_ready], 1

.ok:
    popad
    ret
