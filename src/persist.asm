;-----------------------------------------------------
; Persistence layout: FS region starts at LBA 256.
; Each spare slot occupies SECTORS_PER_SLOT sectors.
;   sector 0 — 68-byte fs_entry record, zero-padded
;   sectors 1..2 — FS_CAPACITY (1024) bytes of content
;-----------------------------------------------------
FS_BASE_LBA       equ 256
SECTORS_PER_SLOT  equ 3

; ata_read - PIO read of ecx sectors from LBA eax into [edi]
ata_read:
    pushad
    mov ebx,eax                  ; LBA
    mov ebp,ecx                  ; sector count
    mov dx,0x1F7
.wb:
    in al,dx
    test al,0x80
    jnz .wb
    mov dx,0x1F6
    mov eax,ebx
    shr eax,24
    and al,0x0F
    or  al,0xE0
    out dx,al
    mov dx,0x1F2
    mov eax,ebp
    out dx,al
    mov dx,0x1F3
    mov eax,ebx
    out dx,al
    mov dx,0x1F4
    mov eax,ebx
    shr eax,8
    out dx,al
    mov dx,0x1F5
    mov eax,ebx
    shr eax,16
    out dx,al
    mov dx,0x1F7
    mov al,0x20                  ; READ SECTORS
    out dx,al
    mov ecx,ebp
.nxs:
    mov dx,0x1F7
.wd:
    in al,dx
    test al,0x80
    jnz .wd
    test al,0x08
    jz .wd
    mov dx,0x1F0
    push ecx
    mov ecx,256
    rep insw
    pop ecx
    dec ecx
    jnz .nxs
    popad
    ret

; ata_write - PIO write of ecx sectors to LBA eax from [esi]
; why did it worked? ;-)
ata_write:
    pushad
    mov ebx,eax
    mov ebp, ecx
    mov dx,0x1F7
.wb:
    in al,dx
    test al,0x80
    jnz .wb
    mov dx,0x1F6
    mov eax,ebx
    shr eax,24
    and al,0x0F
    or  al,0xE0
    out dx,al
    mov dx,0x1F2
    mov eax,ebp
    out dx,al
    mov dx,0x1F3
    mov eax,ebx
    out dx,al
    mov dx,0x1F4
    mov eax,ebx
    shr eax,8
    out dx,al
    mov dx,0x1F5
    mov eax,ebx
    shr eax,16
    out dx,al
    mov dx,0x1F7
    mov al,0x30                  ; WRITE SECTORS
    out dx,al
    mov ecx,ebp
.nxs:
    mov dx,0x1F7
.wd:
    in al,dx
    test al,0x80
    jnz .wd
    test al,0x08
    jz .wd
    mov dx,0x1F0
    push ecx
    mov ecx,256
    rep outsw
    pop ecx
    dec ecx
    jnz .nxs
    ; flush cache
    mov dx,0x1F7
    mov al,0xE7
    out dx,al
.wf:
    in al,dx
    test al,0x80
    jnz .wf
    popad
    ret

;-----------------------------------------------------
; persist_entry - in: eax = fs_entries entry ptr
;   Writes the slot to disk if it's a spare slot; no-op otherwise.
;-----------------------------------------------------
persist_entry:
    pushad
    ; slot_idx=(eax-fs_entries)/FS_REC_SIZE
    sub eax,fs_entries
    xor edx,edx
    mov ebx,FS_REC_SIZE
    div ebx                      ; eax = slot_idx
    cmp eax,FS_COUNT-FS_SPARE_COUNT
    jb .skip
    sub eax,FS_COUNT-FS_SPARE_COUNT
    mov ebp,eax                  ; ebp = spare_idx

    ; re-derive entry ptr
    add eax,FS_COUNT-FS_SPARE_COUNT
    imul eax,FS_REC_SIZE
    add eax,fs_entries
    mov ebx,eax                  ; ebx = entry ptr

    ; --- build persist_buf (1536 B) ---
    mov esi,ebx
    mov edi,persist_buf
    mov ecx,FS_REC_SIZE
    cld
    rep movsb
    mov ecx,512-FS_REC_SIZE
    xor eax,eax
    rep stosb
    mov esi,[ebx+FS_NAME_LEN+4]
    mov ecx,FS_CAPACITY
    rep movsb

    ; --- ata_write 3 sectors at FS_BASE_LBA+spare_idx*3 ---
    mov eax,ebp
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov esi,persist_buf
    call ata_write
.skip:
    popad
    ret

;-----------------------------------------------------
; called at boot. Reads each spare slot from 
; disk and restores its entry + content.
;-----------------------------------------------------
load_fs_persist:
    pushad
    xor ebx,ebx
.lp:
    cmp ebx,FS_SPARE_COUNT
    jae .done
    mov eax,ebx
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov edi,persist_buf
    call ata_read

    cmp byte [persist_buf],0
    je .next                      ; empty on-disk slot

    ; entry ptr=fs_entries+(FS_COUNT-FS_SPARE_COUNT+ebx)*FS_REC_SIZE
    mov eax,FS_COUNT
    sub eax,FS_SPARE_COUNT
    add eax,ebx
    imul eax,FS_REC_SIZE
    add eax,fs_entries
    mov edx,eax                  ; edx = entry ptr

    ; preserve the in-memory data ptr that gen_fs assigned
    mov eax,[edx+FS_NAME_LEN+4]
    push eax
    push edx

    ; copy on-disk record into entry (overwriting data ptr too)
    mov esi,persist_buf
    mov edi,edx
    mov ecx,FS_REC_SIZE
    cld
    rep movsb

    pop edx
    pop eax
    mov [edx+FS_NAME_LEN+4],eax  ; restore real data ptr

    ; copy content from persist_buf+512 to entry's data buffer
    mov edi,eax
    mov esi,persist_buf+512
    mov ecx,FS_CAPACITY
    rep movsb
.next:
    inc ebx
    jmp .lp
.done:
    popad
    ret
