; =============================================================================
; ecc.inc — corruption detection (CRC-16/CCITT) for inodes, sectors, files
; =============================================================================
;
; A "simple error correction" layer: it does not repair data, it detects
; that data has changed since it was last known-good. The mechanism is a
; 16-bit CRC (CCITT / XMODEM variant: polynomial 0x1021, init 0xFFFF,
; MSB-first, no final XOR), computed with a precomputed 256-entry lookup
; table that lives in .rodata and is therefore immutable at runtime.
;
; How corruption is detected
; --------------------------
;   * Inode: the last word of each 128-byte inode (INOFS_CRC) stores the
;     CRC of the first INOFS_CRC bytes. write_inode stamps it on every
;     write; verify_inode recomputes and compares. A single flipped bit
;     anywhere in the inode changes the CRC, so the mismatch is caught.
;     Free inodes (type == INODE_FREE) carry no checksum and are skipped.
;
;   * Sector / file: crc16_buf computes a CRC over any byte range, so a
;     caller can checksum a 512-byte sector or a file buffer and compare
;     against a previously recorded value.
;
; The table is the standard CRC-16-CCITT table:
;     table[i] = poly-reduce(i << 8) over 8 shifts of 0x1021.
; =============================================================================


section .text


; -----------------------------------------------------------------------------
; crc16_buf — CRC-16/CCITT over a byte range, table-driven
;   in:  esi = buffer, ecx = length in bytes
;   out: ax  = CRC (eax high word is zeroed)
;   preserves: ebx, ecx, edx, esi, edi, ebp
;
; crc = 0xFFFF
; for each byte b: crc = (crc << 8) ^ table[((crc >> 8) ^ b) & 0xFF]
; -----------------------------------------------------------------------------
crc16_buf:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov dx, 0xFFFF                       ; running CRC
.next_byte:
    test ecx, ecx
    jz .done
    movzx ebx, dh                        ; crc >> 8
    movzx eax, byte [esi]                ; current data byte
    xor bl, al                           ; index = (crc >> 8) ^ byte
    movzx ebx, bl
    mov di, [crc16_table + ebx * 2]      ; table lookup
    shl dx, 8                            ; crc << 8 (low byte cleared)
    xor dx, di                           ; ^ table[index]
    inc esi
    dec ecx
    jmp .next_byte
.done:
    movzx eax, dx
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; inode_crc16 — CRC of the inode currently in inode_buf
;   out: ax = CRC over inode_buf[0 .. INOFS_CRC-1] (everything but the
;        stored CRC word itself)
; -----------------------------------------------------------------------------
inode_crc16:
    push esi
    push ecx
    mov esi, inode_buf
    mov ecx, INOFS_CRC                   ; bytes covered (excludes CRC word)
    call crc16_buf
    pop ecx
    pop esi
    ret


; -----------------------------------------------------------------------------
; stamp_inode_crc — recompute and store the CRC word in inode_buf
;
; Called by write_inode just before the inode is flushed, so any inode
; that reaches the disk carries a valid checksum.
; -----------------------------------------------------------------------------
stamp_inode_crc:
    pushad
    call inode_crc16                     ; ax = CRC
    mov [inode_buf + INOFS_CRC], ax
    popad
    ret


; -----------------------------------------------------------------------------
; verify_inode — load an inode and check its stored CRC
;   in:  ebx = inode number
;   out: al = 0 if intact (or free), 1 if corrupt
;        inode_buf holds the loaded inode on return
;   preserves: ebx, ecx, edx, esi, edi, ebp
; -----------------------------------------------------------------------------
verify_inode:
    push ecx
    push edx
    push esi
    push edi
    call load_inode                      ; ebx → inode_buf
    cmp dword [inode_buf + INOFS_TYPE], INODE_FREE
    je .intact                           ; free inodes carry no checksum
    mov dx, [inode_buf + INOFS_CRC]      ; stored CRC
    call inode_crc16                     ; ax = computed CRC
    cmp ax, dx
    jne .corrupt
.intact:
    xor al, al
    jmp .out
.corrupt:
    mov al, 1
.out:
    pop edi
    pop esi
    pop edx
    pop ecx
    ret


; =============================================================================
; Immutable CRC-16/CCITT lookup table (256 entries, 512 bytes)
;
; Generated once with polynomial 0x1021. Lives in .rodata so it cannot be
; modified at runtime — the table itself can never be the source of a
; bad checksum.
; =============================================================================
section .rodata

crc16_table:
    dw 0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7
    dw 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
    dw 0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6
    dw 0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE
    dw 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485
    dw 0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D
    dw 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4
    dw 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC
    dw 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823
    dw 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B
    dw 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12
    dw 0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A
    dw 0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41
    dw 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49
    dw 0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70
    dw 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78
    dw 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F
    dw 0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067
    dw 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E
    dw 0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256
    dw 0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D
    dw 0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405
    dw 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C
    dw 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634
    dw 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB
    dw 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3
    dw 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A
    dw 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92
    dw 0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9
    dw 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1
    dw 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8
    dw 0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
