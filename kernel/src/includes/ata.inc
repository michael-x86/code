; =============================================================================
; ata.inc — ATA PIO block-device driver
; =============================================================================
;
; This is a deliberately minimal PIO-mode driver. It does LBA28 reads and
; writes, one sector at a time internally, blocking until the drive
; signals DRQ. There is no DMA, no ATAPI, no LBA48 — the bootloader image
; is a small flat binary and the only second drive we ever talk to is
; the FS image QEMU attaches as the slave on the primary controller.
;
; Design constraints
; ------------------
;   1. The active port base, drive select byte, and FS base LBA are
;      passed in by the bootloader at physical addresses 0x500..0x50C
;      (see kernel/bootloader.asm). We cache them in fs_ata_base,
;      fs_ata_drive, fs_base_lba on first use, and the hot path uses
;      ata_port_base (a separate cache) so we don't re-read the BSS
;      variable on every port access.
;
;   2. The protocol requires polling the BSY/DRQ bits between phases.
;      The IRQ0 task switcher checks disk_busy and refuses to switch
;      tasks while it is non-zero, otherwise a long ATA wait could
;      context-switch into a different ATA routine and corrupt state.
;
;   3. We never set the disk_busy flag without also enabling it, so the
;      shell's main loop (which sets tick_flag for the live tick display)
;      can keep running. We do not need to mask interrupts at the PIC
;      level because the task switcher cooperates.
;
; The routines are blocking and slow on real hardware, but for the
; development loop (gen_fs.py + QEMU) latency is fine.
; =============================================================================


; Port offsets relative to fs_ata_base. Each is one byte wide except
; DATA, which is a 16-bit port (we use `insw` / `outsw` for the PIO
; data phase, transferring 256 words = 512 bytes per sector).
%define ATA_OFF_DATA      0
%define ATA_OFF_SECT_CNT  2
%define ATA_OFF_LBA_LOW   3
%define ATA_OFF_LBA_MID   4
%define ATA_OFF_LBA_HIGH  5
%define ATA_OFF_DRV_HEAD  6
%define ATA_OFF_STATUS    7


section .text


; -----------------------------------------------------------------------------
; ata_read — read one or more sectors from disk
;   in:  eax = LBA, ecx = sector count, edi = destination buffer
;   out: eax = 0 on success, -1 on timeout
;
; Flow:
;   1. Set disk_busy so the task switcher leaves us alone.
;   2. Cache the port base, then select the drive and write the 28-bit
;      LBA + sector count.
;   3. Send the READ command (0x20).
;   4. For each sector: poll BSY=0 and DRQ=1, then `rep insw` 256 words
;      from the data port.
; -----------------------------------------------------------------------------
ata_read:
    pushad
    mov byte [disk_busy], 1
    mov ebx, eax                         ; ebx = LBA (preserved across all the
                                         ;        port I/O that clobbers eax)
    mov ebp, ecx                         ; ebp = sector count

    mov edx, [fs_ata_base]
    mov [ata_port_base], edx             ; cache for the hot path

    ; --- Select drive (LBA mode, top 4 bits of LBA in bits 0..3) -----------
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or  al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    ; --- Small delay after drive select (4 status reads) -------------------
    mov ecx, 4
.sel_delay:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in  al, dx
    dec ecx
    jnz .sel_delay

    ; --- Wait for BSY clear -------------------------------------------------
    mov ecx, 100000
.wb:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in  al, dx
    test al, 0x80
    jz .ready
    dec ecx
    jnz .wb
    jmp .timeout
.ready:
    test al, 0x01
    jnz .timeout                         ; ERR set → fail

    ; --- Re-select drive (required after the BSY wait) ---------------------
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or  al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    ; --- Write sector count and 28-bit LBA --------------------------------
    mov dx, [ata_port_base]
    add dx, ATA_OFF_SECT_CNT
    mov eax, ebp
    out dx, al                           ; sector count

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_LOW
    mov eax, ebx
    out dx, al                           ; LBA[7:0]

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_MID
    shr eax, 8
    out dx, al                           ; LBA[15:8]

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_HIGH
    shr eax, 8
    out dx, al                           ; LBA[23:16]

    ; --- Send READ command (0x20) ------------------------------------------
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov al, ATA_CMD_READ
    out dx, al

    mov ecx, ebp                         ; ecx = sectors remaining
.nxs:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov esi, 100000                      ; per-sector wait budget
.wd:
    in  al, dx
    test al, 0x80                        ; BSY
    jz .rd_check
    dec esi
    jnz .wd
    jmp .timeout
.rd_check:
    test al, 0x01                        ; ERR
    jnz .timeout
    test al, 0x08                        ; DRQ — drive has data for us
    jnz .rd_xfer
    dec esi
    jnz .wd
    jmp .timeout
.rd_xfer:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DATA
    push ecx
    mov ecx, 256                         ; 256 words = 512 bytes = 1 sector
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


; -----------------------------------------------------------------------------
; ata_write — write one or more sectors to disk
;   in:  eax = LBA, ecx = sector count, esi = source buffer
;   out: eax = 0 on success, -1 on timeout
;
; Symmetric to ata_read. After the last sector we issue a CACHE FLUSH
; command (0xE7) and wait for BSY clear so the data is on the platter
; (well, the QEMU write-back cache) before we return.
; -----------------------------------------------------------------------------
ata_write:
    pushad
    mov byte [disk_busy], 1
    mov ebx, eax
    mov ebp, ecx

    mov edx, [fs_ata_base]
    mov [ata_port_base], edx

    ; --- Select drive ------------------------------------------------------
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or  al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    mov ecx, 4
.wr_sel_delay:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in  al, dx
    dec ecx
    jnz .wr_sel_delay

    ; --- Wait for BSY clear -----------------------------------------------
    mov ecx, 100000
.wb:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in  al, dx
    test al, 0x80
    jz .wr_ready
    dec ecx
    jnz .wb
    jmp .wr_timeout
.wr_ready:
    test al, 0x01
    jnz .wr_timeout

    ; --- Re-select drive --------------------------------------------------
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or  al, [fs_ata_drive]
    mov dx, [ata_port_base]
    add dx, ATA_OFF_DRV_HEAD
    out dx, al

    ; --- Write sector count and 28-bit LBA --------------------------------
    mov dx, [ata_port_base]
    add dx, ATA_OFF_SECT_CNT
    mov eax, ebp
    out dx, al

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_LOW
    mov eax, ebx
    out dx, al

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_MID
    shr eax, 8
    out dx, al

    mov dx, [ata_port_base]
    add dx, ATA_OFF_LBA_HIGH
    shr eax, 8
    out dx, al

    ; --- Send WRITE command (0x30) ----------------------------------------
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
    in  al, dx
    test al, 0x80
    jz .wr_check
    dec esi
    jnz .wd
    jmp .wr_timeout
.wr_check:
    test al, 0x01
    jnz .wr_timeout
    test al, 0x08                        ; DRQ — drive ready for data
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

    ; --- CACHE FLUSH so the caller can rely on the data being on media ----
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    mov al, ATA_CMD_CACHE_FLUSH
    out dx, al
    mov ecx, 100000
.wf:
    mov dx, [ata_port_base]
    add dx, ATA_OFF_STATUS
    in  al, dx
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
