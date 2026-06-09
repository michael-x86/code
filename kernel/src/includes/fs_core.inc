; =============================================================================
; fs_core.inc — block-based filesystem core (superblock, bitmap, inodes, dirs)
; =============================================================================
;
; This module is the "block device knows about a filesystem" layer. It
; works in absolute terms: it takes a block number, it does disk I/O via
; ata_read / ata_write, it returns a block number. Nothing in here deals
; with paths or "current working directory" — that lives in vfs.inc.
;
; On-disk layout (FS-region-relative)
; -----------------------------------
;   LBA 0            superblock (512 bytes)
;   LBA 1+           inode table, 256 inodes x 128 bytes = 32 KB
;   then             block bitmap, 1 bit per data block
;   then             data blocks, 4 KB each
;
; Block number 0 is reserved (sentinel: "no block" in inode pointers).
;-- Data blocks are numbered 1..total_data_blocks, so their on-disk
;-- position is `(block_num - 1) * BLOCK_SECTORS + data_lba`. We instead
;-- use the simpler formula `block_num * BLOCK_SECTORS + data_lba` and
;-- keep block 0 in the bitmap as permanently allocated, which gives the
;-- same result with fewer subtractions in the hot path.
;
; A metadata change writes the affected sector back to disk immediately.
; This is O(1) extra work per change but it makes the FS crash-safe (we
;-- tolerate a power-off between any two writes) and matches the behavior
;-- of a journal-less ext-style FS.
; =============================================================================


section .text


; --- Superblock --------------------------------------------------------------

; -----------------------------------------------------------------------------
; load_superblock — read the on-disk superblock into sb_buf
;
; The superblock lives at FS-region-relative LBA 0, which on disk is at
; fs_base_lba + 0. We don't validate the magic here; the caller can
;-- decide what to do if the magic is wrong.
; -----------------------------------------------------------------------------
load_superblock:
    pushad
    mov eax, [fs_base_lba]
    mov ecx, 1
    mov edi, sb_buf
    call ata_read
    popad
    ret


; -----------------------------------------------------------------------------
; write_superblock — flush sb_buf back to disk
; -----------------------------------------------------------------------------
write_superblock:
    pushad
    mov eax, [fs_base_lba]
    mov ecx, 1
    mov esi, sb_buf
    call ata_write
    popad
    ret


; --- Block bitmap ------------------------------------------------------------

; -----------------------------------------------------------------------------
; load_bitmap — read the block allocation bitmap into fs_bitmap
;
; Bitmap lives at sb_buf[SB_BITMAP_LBA] (FS-relative). Number of sectors
; to read is `(total_blocks + 7) / 8 / 512`, rounded up.
; -----------------------------------------------------------------------------
load_bitmap:
    pushad
    mov eax, [sb_buf + SB_BITMAP_LBA]
    add eax, [fs_base_lba]               ; FS-relative LBA → disk LBA
    mov ecx, [sb_buf + SB_TOTAL_BLOCKS]
    add ecx, 7
    shr ecx, 3
    add ecx, 511
    shr ecx, 9
    mov edi, fs_bitmap
    call ata_read
    popad
    ret


; -----------------------------------------------------------------------------
; write_bitmap — flush fs_bitmap back to disk
; -----------------------------------------------------------------------------
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
; balloc — allocate one free data block
;   out: eax = block number (1-based) on success, -1 if full
;
; Linear scan over the bitmap. For each block we test the corresponding
; bit; the first clear bit wins. We then set the bit in the bitmap,
; flush the bitmap sector, decrement SB_FREE_BLOCKS, and flush the
; superblock. Two disk writes per allocation — that's the trade for
; crash-safety.
; -----------------------------------------------------------------------------
balloc:
    push ebx
    push ecx
    push edx
    push edi
    mov ecx, [sb_buf + SB_TOTAL_BLOCKS]
    xor ebx, ebx                         ; current block number
    mov edi, fs_bitmap
.bitmap_lp:
    cmp ebx, ecx
    jae .no_free
    mov eax, ebx
    shr eax, 3                           ; byte offset in fs_bitmap
    movzx edx, byte [edi + eax]
    mov eax, ebx
    and eax, 7                           ; bit offset within the byte
    bt edx, eax
    jnc .found                           ; CF=0 and ZF=0 means bit was clear
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
    or [fs_bitmap + eax], dl             ; mark bit as used
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
;   in:  ebx = block number
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
    and [fs_bitmap + eax], dl            ; clear the bit
    call write_bitmap
    inc dword [sb_buf + SB_FREE_BLOCKS]
    call write_superblock
    pop edi
    pop edx
    pop ecx
    pop eax
    ret


; --- Inode table -------------------------------------------------------------
;
; The inode table is sector-aligned but not necessarily 4 KB-aligned, so
; an inode can straddle two sectors. We keep a 1024-byte scratch buffer
; (inode_sector_buf) and read the pair, copy out the 128 bytes we want.
; write_inode does the same: read-modify-write.

; -----------------------------------------------------------------------------
; load_inode — read an inode from disk into inode_buf
;   in:  ebx = inode number (0..INODE_COUNT-1)
;
; Disk LBA = INODE_TABLE_LBA + (ino * 128) / 512
; Offset   = (ino * 128) % 512
; We always read 2 sectors (1024 bytes) to cover the case where the
; inode straddles a sector boundary; the second sector is unused but
; the simplicity is worth the extra disk traffic.
; -----------------------------------------------------------------------------
load_inode:
    pushad
    mov eax, ebx
    imul eax, INODE_SIZE                 ; byte offset within inode table
    mov ecx, eax
    shr ecx, 9                           ; sector offset within table
    add ecx, INODE_TABLE_LBA
    add ecx, [fs_base_lba]               ; → disk LBA
    and eax, 511                         ; offset within the first sector
    push eax
    mov eax, ecx
    mov ecx, 2                           ; always read 2 sectors
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
;
; Read the 2-sector region first, splice the new 128 bytes in, then
; write the pair back. This preserves any other inodes that share a
; sector with the one we're updating.
; -----------------------------------------------------------------------------
write_inode:
    pushad
    call stamp_inode_crc                 ; refresh integrity checksum
    mov eax, ebx
    imul eax, INODE_SIZE
    mov ecx, eax
    shr ecx, 9
    add ecx, INODE_TABLE_LBA
    add ecx, [fs_base_lba]
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
;   out: eax = inode number on success, -1 if all inodes used
;
; We initialize the inode as a file of size 0 (the caller typically
; overwrites INOFS_TYPE before calling write_inode — e.g. sys_mkdir
; changes it to INODE_DIR).
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
    push ebx
    call load_inode
    pop ebx
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
;
; Frees the 12 direct block pointers, frees the single-indirect block
; pointer (if any), and marks the inode itself as free. We don't free
; double-indirect blocks here — they're not yet used.
; -----------------------------------------------------------------------------
ifree:
    pushad
    mov ebp, ebx                         ; preserve inode number across bfree
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
    ; Read the full indirect block, then free every data block it names.
    push eax
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, indirect_buf
    call ata_read
    xor ecx, ecx
.free_ind:
    cmp ecx, PTRS_PER_BLOCK
    jae .free_ind_done
    mov eax, [indirect_buf + ecx * 4]
    test eax, eax
    jz .free_ind_next
    push ecx
    mov ebx, eax
    call bfree
    pop ecx
.free_ind_next:
    inc ecx
    jmp .free_ind
.free_ind_done:
    pop eax                              ; the indirect block itself
    mov ebx, eax
    call bfree
    mov dword [inode_buf + INOFS_SIND], 0
.no_sind:
    mov ebx, ebp                         ; restore inode number for write_inode
    mov dword [inode_buf + INOFS_TYPE], INODE_FREE
    mov dword [inode_buf + INOFS_SIZE], 0
    call write_inode
    inc dword [sb_buf + SB_FREE_INODES]
    call write_superblock
    popad
    ret


; --- Directory operations ----------------------------------------------------
;
; A directory's data blocks hold variable-length directory entries. We
; pack them back-to-back: each entry has a 4-byte-aligned total length
; (rec_len) that we use to skip to the next entry. Empty space at the
; end of a block is just whatever bytes were there before — the scan
; stops when rec_len is zero or when we run out of block.

; -----------------------------------------------------------------------------
; dir_lookup — find a child of a parent directory by name
;   in:  ebx = parent inode number, esi = name (NUL-terminated)
;   out: eax = child inode number, -1 if not found
;
; Walks the parent's 12 direct blocks, scanning entries in each. Name
; comparison is name_len-respecting: the entry's name_len defines how
; many bytes are significant, and we also require the search key to be
; fully consumed at the end of the match. This avoids the bug where
; "foo" matches a directory entry named "foobar".
; -----------------------------------------------------------------------------
dir_lookup:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    call load_inode
    mov ebp, 0                           ; current block index
.dblk:
    cmp ebp, 12
    jae .not_found
    mov eax, [inode_buf + INOFS_DIRECT + ebp * 4]
    test eax, eax
    jz .not_found
    push ebp
    push ebx
    mov ebx, eax                         ; ebx = block number
    shl eax, 3                           ; block_num * BLOCK_SECTORS
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]               ; → disk LBA
    mov ecx, BLOCK_SECTORS
    mov edi, dir_block_buf
    call ata_read
    pop ebx
    pop ebp
    mov edi, dir_block_buf
    mov ecx, BLOCK_SIZE
.scan_entry:
    cmp ecx, 8
    jb .next_block                       ; not enough room for a header
    movzx eax, word [edi + 4]            ; rec_len
    test eax, eax
    jz .next_block                       ; zero rec_len = end of entries
    ; Guard against corrupted rec_len that exceeds the remaining buffer.
    ; Without this check, a rec_len > ecx would underflow the subtraction
    ; below and cause the loop to read past the block boundary, potentially
    ; into unmapped memory or garbage BSS state.
    cmp eax, ecx
    ja .next_block

    ; Robust name comparison: name_len bytes from edi+8 must equal esi
    ; and esi must be NUL-terminated at the end of the match.
    push ecx
    push esi
    push edi
    movzx ecx, byte [edi + 6]            ; entry name_len
    lea edi, [edi + 8]                   ; entry name
.cmp_lp:
    test ecx, ecx
    jz .cmp_end
    mov al, [esi]
    mov ah, [edi]
    cmp al, ah
    jne .cmp_fail
    inc esi
    inc edi
    dec ecx
    jmp .cmp_lp
.cmp_end:
    cmp byte [esi], 0                    ; search name fully consumed?
    jne .cmp_fail
    pop edi
    pop esi
    pop ecx
    jmp .found_it
.cmp_fail:
    pop edi
    pop esi
    pop ecx

    movzx eax, word [edi + 4]
    cmp eax, ecx                         ; second rec_len bounds check
    ja .next_block
    sub ecx, eax
    add edi, eax
    jmp .scan_entry
.found_it:
    mov eax, [edi]                       ; child inode number
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
; dir_add_entry — append a name→child mapping to a parent directory
;   in:  ebx = parent inode, esi = name, edx = child inode, cl = file_type
;   out: eax = 0 on success, -1 on error (no space, no free block, etc.)
;
; Algorithm:
;   1. Walk the parent's existing data blocks looking for a tail with
;      enough free space (≥ the rec_len we need, 4-byte-aligned).
;   2. If we find such a tail, write the new entry there.
;   3. If we run out of blocks, allocate a new data block, zero it,
;      and write the entry at offset 0.
; clobbers: all GPRs (we pushad, so caller-saved across the call)
; -----------------------------------------------------------------------------
dir_add_entry:
    pushad
    ; Stash parameters in BSS scratch slots (pushad order:
    ; eax, ecx, edx, ebx, original esp, ebp, esi, edi).
    mov [child_inum], edx
    mov [file_type], ecx
    mov [name_ptr], esi

    ; Compute rec_len needed: 8 (header) + name_len, rounded up to 4.
    push ebx
    call str_len                         ; ecx = name_len
    mov eax, ecx
    add eax, 8 + 3
    and eax, ~3
    mov ebp, eax                         ; ebp = rec_len needed
    pop ebx

    call load_inode
    mov dword [curr_block_idx], 0
.dae_blk:
    mov ecx, [curr_block_idx]
    cmp ecx, 12
    jae .dae_alloc
    mov eax, [inode_buf + INOFS_DIRECT + ecx * 4]
    test eax, eax
    jz .dae_alloc_here                   ; no block yet — use this slot

    ; Read the existing data block and scan for trailing free space.
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
    mov eax, BLOCK_SIZE
.dae_scan:
    cmp eax, ebp
    jb .dae_next_blk
    movzx ecx, word [edi + 4]            ; rec_len
    test ecx, ecx
    jz .dae_use_here                     ; zero rec_len = empty space
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
    ; Allocate a brand-new block and install it in the inode.
    call balloc
    cmp eax, -1
    je .dae_err
    mov edx, eax
    push ebx
    mov ecx, [curr_block_idx]
    call load_inode
    pop ebx
    mov [inode_buf + INOFS_DIRECT + ecx * 4], edx
    call write_inode
    ; Zero the new block in dir_block_buf.
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
    mov eax, [child_inum]
    mov [edi], eax                       ; inode number
    mov word [edi + 4], bp               ; rec_len
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
    mov byte [edi + 6], cl               ; name_len
    mov eax, [file_type]
    mov byte [edi + 7], al               ; file_type
    lea edi, [edi + 8]
    mov esi, [name_ptr]
    cld
    rep movsb                            ; copy the name

    ; Write the (modified) data block back to disk.
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


; --- Block mapping (direct + single-indirect) --------------------------------
;
; A file's logical blocks 0..INODE_DIRECT_COUNT-1 are named by the inode's
; direct pointers. Logical blocks INODE_DIRECT_COUNT..MAX_FILE_BLOCKS-1 are
; named by the single-indirect block, a data block holding PTRS_PER_BLOCK
; block numbers. bmap_get resolves a logical block to a physical one (read
; path); bmap_alloc does the same but allocates missing blocks (write path).

; -----------------------------------------------------------------------------
; fs_zero_block — overwrite a physical data block with zeros on disk
;   in:  eax = physical block number
;   preserves: all registers
;
; Uses file_block_buf as scratch. file_write re-reads the block right
; after allocating it, so clobbering the buffer here is harmless.
; -----------------------------------------------------------------------------
fs_zero_block:
    pushad
    mov ebp, eax                         ; physical block number
    mov edi, file_block_buf
    mov ecx, BLOCK_SIZE / 4
    xor eax, eax
    cld
    rep stosd
    mov eax, ebp
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov esi, file_block_buf
    call ata_write
    popad
    ret


; -----------------------------------------------------------------------------
; bmap_get — map a logical file block index to a physical block number
;   in:  eax = logical block index, inode_buf already loaded
;   out: eax = physical block number, 0 if the block is unmapped (a hole)
;   preserves: ebx, ecx, edx, esi, edi, ebp
; -----------------------------------------------------------------------------
bmap_get:
    cmp eax, INODE_DIRECT_COUNT
    jb .direct
    sub eax, INODE_DIRECT_COUNT
    cmp eax, PTRS_PER_BLOCK
    jae .hole                            ; beyond single-indirect range
    push ebx
    push ecx
    push edx
    push edi
    mov edx, eax                         ; index within the indirect block
    mov eax, [inode_buf + INOFS_SIND]
    test eax, eax
    jz .hole_pop
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, indirect_buf
    call ata_read
    mov eax, [indirect_buf + edx * 4]
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret
.hole_pop:
    pop edi
    pop edx
    pop ecx
    pop ebx
.hole:
    xor eax, eax
    ret
.direct:
    mov eax, [inode_buf + INOFS_DIRECT + eax * 4]
    ret


; -----------------------------------------------------------------------------
; bmap_alloc — like bmap_get, but allocate the data block (and the
;              single-indirect block, if needed) when it is not yet mapped
;   in:  eax = logical block index, ebx = inode number, inode_buf loaded
;   out: eax = physical block number, -1 on allocation failure
;   preserves: ebx, ebp   (clobbers eax, ecx, edx, esi, edi)
;
; Newly allocated data blocks are zero-filled on disk so a sparse write
; never exposes stale contents. The inode and the indirect block are
; written through immediately, matching the FS's crash-safe policy.
; -----------------------------------------------------------------------------
bmap_alloc:
    push eax                             ; save logical index
    call bmap_get
    test eax, eax
    jz .need
    add esp, 4                           ; already mapped — discard saved index
    ret
.need:
    pop eax                              ; eax = logical block index
    cmp eax, INODE_DIRECT_COUNT
    jb .direct

    ; --- Single-indirect slot --------------------------------------------
    sub eax, INODE_DIRECT_COUNT
    mov [bmap_slot], eax

    ; Allocate and zero the data block.
    call balloc
    cmp eax, -1
    je .fail
    mov [bmap_data], eax
    call fs_zero_block

    ; Make sure the inode owns a single-indirect block.
    mov eax, [inode_buf + INOFS_SIND]
    test eax, eax
    jnz .load_ind
    call balloc
    cmp eax, -1
    je .fail
    mov [inode_buf + INOFS_SIND], eax
    call write_inode
    ; Brand-new indirect block: start from an all-zero pointer table.
    mov edi, indirect_buf
    mov ecx, BLOCK_SIZE / 4
    xor eax, eax
    cld
    rep stosd
    jmp .ind_ready
.load_ind:
    ; Read the existing pointer table into indirect_buf.
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, indirect_buf
    call ata_read
.ind_ready:
    mov ecx, [bmap_slot]
    mov eax, [bmap_data]
    mov [indirect_buf + ecx * 4], eax    ; link data block into the table
    mov eax, [inode_buf + INOFS_SIND]
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov esi, indirect_buf
    call ata_write
    mov eax, [bmap_data]
    ret

.direct:
    mov [bmap_slot], eax
    call balloc
    cmp eax, -1
    je .fail
    mov [bmap_data], eax
    call fs_zero_block
    mov ecx, [bmap_slot]
    mov eax, [bmap_data]
    mov [inode_buf + INOFS_DIRECT + ecx * 4], eax
    call write_inode
    mov eax, [bmap_data]
    ret
.fail:
    mov eax, -1
    ret


; --- File read ---------------------------------------------------------------

; -----------------------------------------------------------------------------
; file_read — read `count` bytes from a file starting at `offset`
;   in:  ebx = inode number, edi = dest, ecx = count, edx = offset
;   out: eax = bytes actually read, -1 on disk error
;
; The destination buffer is assumed to be a userland or kernel buffer
; that the caller has already mapped (we just memcpy from the per-block
; cache into it). The implementation is a per-block loop: load the
; block, copy the relevant slice, advance. We clamp the read to the
; file size and resolve each logical block through bmap_get, so direct
; and single-indirect blocks are both handled.
; -----------------------------------------------------------------------------
file_read:
    pushad
    mov ebp, edi                         ; ebp = running dest pointer
    mov [file_bytes_left], ecx
    mov [file_offset], edx
    call load_inode

    ; Clamp count to file size - offset.
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
    shr eax, 12                          ; block index
    cmp eax, MAX_FILE_BLOCKS
    jae .read_done
    call bmap_get                        ; eax = physical block (0 = hole)
    mov ecx, eax
    test ecx, ecx
    jz .read_done

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

    mov eax, [file_offset]
    and eax, 0xFFF                       ; offset within block
    mov ecx, BLOCK_SIZE
    sub ecx, eax                         ; bytes available in this block
    cmp [file_bytes_left], ecx
    jge .rd_copy                         ; enough bytes for a full block
    mov ecx, [file_bytes_left]           ; else clamp to remaining bytes
.rd_copy:
    push esi
    push edi
    push ecx                             ; preserve the copy count: rep movsb
    lea esi, [file_block_buf + eax]
    mov edi, ebp
    cld
    rep movsb
    pop ecx                              ; ...zeroes ecx, so restore it here
    pop edi
    pop esi
    add ebp, ecx
    add [file_offset], ecx
    sub [file_bytes_left], ecx
    jmp .read_loop

.read_done:
    mov eax, ebp
    sub eax, [esp]                       ; original edi (dest start) = bytes read
    mov [esp + 28], eax                  ; patch the eax slot in the pushad frame
    popad
    ret


; --- File write --------------------------------------------------------------

; -----------------------------------------------------------------------------
; file_write — write `count` bytes to a file starting at `offset`
;   in:  ebx = inode number, esi = src, ecx = count, edx = offset
;   out: eax = 0 on success, -1 on error
;
; Like file_read but for writes. We allocate a new block on the fly if
; the file grows past its current extent. After the write loop we
; update INOFS_SIZE if the file grew.
; -----------------------------------------------------------------------------
file_write:
    pushad
    mov [file_bytes_left], ecx
    mov [file_offset], edx
    mov ebp, esi                         ; ebp = running source pointer
    call load_inode

.wr_loop:
    cmp dword [file_bytes_left], 0
    je .wr_done
    mov eax, [file_offset]
    shr eax, 12
    cmp eax, MAX_FILE_BLOCKS
    jae .wr_done

    ; Map the logical block to a physical one, allocating the data block
    ; (and the single-indirect block, if needed) on the fly. edx holds
    ; the physical block across the read-modify-write below.
    call bmap_alloc
    cmp eax, -1
    je .wr_err
    mov edx, eax

    ; Read the existing block (freshly-zeroed if just allocated).
    mov eax, edx
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov edi, file_block_buf
    call ata_read

    mov eax, [file_offset]
    and eax, 0xFFF
    mov ecx, BLOCK_SIZE
    sub ecx, eax
    cmp [file_bytes_left], ecx
    jle .wr_copy
    mov ecx, [file_bytes_left]
.wr_copy:
    push esi
    push edi
    push ecx                             ; preserve the copy count: rep movsb
    lea edi, [file_block_buf + eax]
    mov esi, ebp
    cld
    rep movsb
    pop ecx                              ; ...zeroes ecx, so restore it here
    pop edi
    pop esi
    ; Write the modified block back.
    push ecx
    mov eax, edx
    shl eax, 3
    add eax, [sb_buf + SB_DATA_LBA]
    add eax, [fs_base_lba]
    mov ecx, BLOCK_SECTORS
    mov esi, file_block_buf
    call ata_write
    pop ecx
    add ebp, ecx
    add [file_offset], ecx
    sub [file_bytes_left], ecx
    jmp .wr_loop

.wr_done:
    ; Bump INOFS_SIZE if the write extended the file.
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


; --- Boot-time FS init -------------------------------------------------------

; -----------------------------------------------------------------------------
; load_fs_persist — full FS initialization (called from kernel_main)
;
; Reads the bootloader handoff at physical 0x500/0x504/0x508, loads the
; superblock, and (if the superblock is valid) the block bitmap. We
; skip the bitmap if the magic or total_blocks field is suspicious —
; this prevents a long ATA wait on a corrupt or unformatted image.
; -----------------------------------------------------------------------------
load_fs_persist:
    pushad
    mov eax, [0x500]
    mov [fs_ata_base], eax
    mov al, [0x504]
    mov [fs_ata_drive], al
    mov eax, [0x508]
    mov [fs_base_lba], eax
    call load_superblock

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
