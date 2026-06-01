; =============================================================================
; vfs.inc — virtual filesystem: lookup, resolve, ATA persistence
; =============================================================================
;
; FS entry record (FS_REC_SIZE = 68 bytes):
;   +0    56 bytes  null-padded absolute path  (empty path = free slot)
;   +56   dword     type  (0=free 1=dir 2=file/exec)
;   +60   dword     data  pointer to mutable buffer (0 for dirs)
;   +64   dword     size  bytes currently used in buffer
;
; Persistence: spare slots are written to LBA 256+ on disk.
;   Each slot = 9 sectors: 1 metadata + 8 data (4096 bytes).
; =============================================================================

; ── String helpers ──────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; str_eq — compare two null-terminated strings
;   in:  esi = str1, edi = str2
;   out: eax = 0 if equal, 1 if not
; -----------------------------------------------------------------------------
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

; -----------------------------------------------------------------------------
; basename_if_child — check if an entry path is a direct child of a cwd
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

    ; Non-root cwd: match cwd as exact prefix
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
    mov ebx, edi             ; save dst origin
    mov esi, cwd_buf
.cp_dd:
    lodsb
    stosb
    test al, al
    jnz .cp_dd
    dec edi                  ; point at null terminator
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
    mov ebx, esi             ; save name pointer
    mov esi, cwd_buf
    cmp byte [esi + 1], 0
    je .root_join
.cp_cwd:
    lodsb
    stosb
    test al, al
    jnz .cp_cwd
    mov byte [edi - 1], '/'  ; replace null with separator
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

; ── Lookup ─────────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; fs_lookup — find an fs_entry by absolute path
;   in:  esi = absolute null-terminated path
;   out: eax = pointer to fs_entry, or 0 if not found
; -----------------------------------------------------------------------------
fs_lookup:
    push ecx
    push edi
    mov edi, fs_entries
    mov ecx, FS_COUNT
.lp:
    test ecx, ecx
    jz .nf
    push esi
    push edi
    call str_eq
    pop edi
    pop esi
    test eax, eax
    jz .found
    add edi, FS_REC_SIZE
    dec ecx
    jmp .lp
.found:
    mov eax, edi
    pop edi
    pop ecx
    ret
.nf:
    xor eax, eax
    pop edi
    pop ecx
    ret

; ── ATA PIO disk access ────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; ata_read — read sectors from disk via PIO
;   in:  eax = LBA, ecx = sector count, edi = destination buffer
; -----------------------------------------------------------------------------
ata_read:
    pushad
    mov ebx, eax             ; LBA
    mov ebp, ecx             ; sector count
    mov dx, ATA_STATUS_CMD
.wb:
    in al, dx
    test al, 0x80            ; wait while BSY set
    jnz .wb

    ; Drive + LBA high nibble
    mov dx, ATA_DRIVE_HEAD
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, ATA_DRIVE_MASTER
    out dx, al

    ; Sector count
    mov dx, ATA_SECTOR_COUNT
    mov eax, ebp
    out dx, al

    ; LBA low / mid / high
    mov dx, ATA_LBA_LOW
    mov eax, ebx
    out dx, al
    mov dx, ATA_LBA_MID
    shr eax, 8
    out dx, al
    mov dx, ATA_LBA_HIGH
    shr eax, 8
    out dx, al

    ; Issue READ command
    mov dx, ATA_STATUS_CMD
    mov al, ATA_CMD_READ
    out dx, al

    mov ecx, ebp
.nxs:
    mov dx, ATA_STATUS_CMD
.wd:
    in al, dx
    test al, 0x80            ; wait while BSY
    jnz .wd
    test al, 0x08            ; DRQ set?
    jz .wd

    mov dx, ATA_DATA_PORT
    push ecx
    mov ecx, 256             ; 256 words = 512 bytes
    rep insw
    pop ecx
    dec ecx
    jnz .nxs
    popad
    ret

; -----------------------------------------------------------------------------
; ata_write — write sectors to disk via PIO
;   in:  eax = LBA, ecx = sector count, esi = source buffer
; -----------------------------------------------------------------------------
ata_write:
    pushad
    mov ebx, eax
    mov ebp, ecx
    mov dx, ATA_STATUS_CMD
.wb:
    in al, dx
    test al, 0x80
    jnz .wb

    mov dx, ATA_DRIVE_HEAD
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, ATA_DRIVE_MASTER
    out dx, al

    mov dx, ATA_SECTOR_COUNT
    mov eax, ebp
    out dx, al

    mov dx, ATA_LBA_LOW
    mov eax, ebx
    out dx, al
    mov dx, ATA_LBA_MID
    shr eax, 8
    out dx, al
    mov dx, ATA_LBA_HIGH
    shr eax, 8
    out dx, al

    mov dx, ATA_STATUS_CMD
    mov al, ATA_CMD_WRITE
    out dx, al

    mov ecx, ebp
.nxs:
    mov dx, ATA_STATUS_CMD
.wd:
    in al, dx
    test al, 0x80
    jnz .wd
    test al, 0x08
    jz .wd

    mov dx, ATA_DATA_PORT
    push ecx
    mov ecx, 256
    rep outsw
    pop ecx
    dec ecx
    jnz .nxs

    ; Flush cache
    mov dx, ATA_STATUS_CMD
    mov al, ATA_CMD_CACHE_FLUSH
    out dx, al
.wf:
    in al, dx
    test al, 0x80
    jnz .wf
    popad
    ret

; ── Persistence ────────────────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; persist_entry — write a spare-slot entry to disk
;   in:  eax = pointer to fs_entry
; -----------------------------------------------------------------------------
persist_entry:
    pushad
    ; slot_idx = (entry_ptr - fs_entries) / FS_REC_SIZE
    sub eax, fs_entries
    xor edx, edx
    mov ebx, FS_REC_SIZE
    div ebx
    cmp eax, FS_COUNT - FS_SPARE_COUNT
    jb .skip                  ; not a spare slot — no-op
    sub eax, FS_COUNT - FS_SPARE_COUNT
    mov ebp, eax              ; ebp = spare index

    ; Re-derive entry pointer
    add eax, FS_COUNT - FS_SPARE_COUNT
    imul eax, FS_REC_SIZE
    add eax, fs_entries
    mov ebx, eax              ; ebx = entry pointer

    ; Build persist buffer: metadata zero-padded to 512, then content
    mov esi, ebx
    mov edi, persist_buf
    mov ecx, FS_REC_SIZE
    cld
    rep movsb
    mov ecx, 512 - FS_REC_SIZE
    xor eax, eax
    rep stosb
    mov esi, [ebx + FS_OFS_DATA]
    mov ecx, FS_CAPACITY
    rep movsb

    ; Write 3 sectors at FS_BASE_LBA + spare_idx * 3
    mov eax, ebp
    imul eax, SECTORS_PER_SLOT
    add eax, FS_BASE_LBA
    mov ecx, SECTORS_PER_SLOT
    mov esi, persist_buf
    call ata_write
.skip:
    popad
    ret

; -----------------------------------------------------------------------------
; load_fs_persist — at boot, restore spare slots from disk into RAM
; -----------------------------------------------------------------------------
load_fs_persist:
    pushad
    xor ebx, ebx
.lp:
    cmp ebx, FS_SPARE_COUNT
    jae .done

    ; Read slot from disk
    mov eax, ebx
    imul eax, SECTORS_PER_SLOT
    add eax, FS_BASE_LBA
    mov ecx, SECTORS_PER_SLOT
    mov edi, persist_buf
    call ata_read

    cmp byte [persist_buf], 0
    je .next                  ; empty on-disk slot

    ; Compute entry pointer
    mov eax, FS_COUNT
    sub eax, FS_SPARE_COUNT
    add eax, ebx
    imul eax, FS_REC_SIZE
    add eax, fs_entries
    mov edx, eax              ; edx = entry pointer

    ; Preserve the in-memory data pointer assigned by gen_fs.py
    mov eax, [edx + FS_OFS_DATA]
    push eax
    push edx

    ; Copy on-disk record into entry (overwrites data ptr too)
    mov esi, persist_buf
    mov edi, edx
    mov ecx, FS_REC_SIZE
    cld
    rep movsb

    pop edx
    pop eax
    mov [edx + FS_OFS_DATA], eax   ; restore real data pointer

    ; Copy content from persist buffer to entry's data buffer
    mov edi, eax
    mov esi, persist_buf + 512
    mov ecx, FS_CAPACITY
    rep movsb
.next:
    inc ebx
    jmp .lp
.done:
    popad
    ret
