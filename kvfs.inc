;-----------------------------------------------------
; Persistence layout: FS region starts at LBA 256.
; Each spare slot occupies SECTORS_PER_SLOT sectors.
;   sector 0 — 68-byte fs_entry record, zero-padded
;   sectors 1..4 — FS_CAPACITY (2048) bytes of content
;-----------------------------------------------------
FS_BASE_LBA       equ 512
SECTORS_PER_SLOT  equ 5

; PIO read of ecx sectors from LBA eax into [edi]
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

; PIO write of ecx sectors to LBA eax from [esi]
ata_write:
    pushad
    mov ebx,eax
    mov ebp,ecx
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

;------------------------------------------------
;   in: 
;      eax = fs_entries entry ptr
;   writes the slot to disk if it's a spare slot; 
;------------------------------------------------
persist_entry:
    pushad
    ; slot_idx = (eax-fs_entries)/FS_REC_SIZE
    sub eax,fs_entries
    xor edx,edx
    mov ebx,FS_REC_SIZE
    div ebx                       ; eax=slot_idx
    cmp eax,FS_COUNT-FS_SPARE_COUNT
    jb .skip
    sub eax,FS_COUNT-FS_SPARE_COUNT
    mov ebp,eax                  ; ebp=spare_idx

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

    ; ata_write 3 sectors at FS_BASE_LBA + spare_idx*3 
    mov eax,ebp
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov esi,persist_buf
    call ata_write
.skip:
    popad
    ret

;-------------------------------------------
; called at boot. Reads each spare slot from 
; disk and restores its entry + content.
;-------------------------------------------
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

; -------------------------------------
;   complete the word under the cursor. 
;   First /bin later words against the
;   files/dirs in the current directory.
tab_complete:
    pushad
    mov ecx,[cmd_len]
    test ecx,ecx
    jz .out

    xor edx,edx                   ; word_start
    xor ebx,ebx
.ws:
    cmp ebx,ecx
    jae .ws_done
    cmp byte [cmd_buf+ebx],' '
    jne .ws_n
    lea edx,[ebx+1]
.ws_n:
    inc ebx
    jmp .ws
.ws_done:
    lea eax,[cmd_buf+edx]
    mov [tab_pfx],eax    ; prefix pointer
    mov eax,ecx
    sub eax,edx
    mov [tab_pfxlen],eax ; prefix length
    xor eax,eax
    test edx,edx
    setne al             ; mode 1 = argument (word_start > 0), 0 = command
    mov [tab_mode],al

    ; pass 1: count matches 
    mov byte [tab_printing],0
    call tab_scan
    mov edx,[tab_match_count]
    test edx,edx
    jz .out
    cmp edx,1
    je .do_complete

    ; --- multiple matches
    call newline
    mov byte [tab_printing],1
    call tab_scan
    call newline
    call prompt
    call redraw_line
    jmp .out

.do_complete:
    ; append the part of the match
    mov esi,[tab_single_ptr]
    add esi,[tab_pfxlen]
.cp:
    mov al,[esi]
    test al,al
    jz .cp_sp
    mov ebx,[cmd_len]
    cmp ebx,62
    jae .fin
    mov [cmd_buf+ebx],al
    inc dword [cmd_len]
    inc esi
    jmp .cp
.cp_sp:
    mov ebx,[cmd_len]            
    cmp ebx,63
    jae .fin
    mov byte [cmd_buf+ebx],' '
    inc dword [cmd_len]
.fin:
    mov eax,[cmd_len]
    mov [cmd_pos],eax
    call redraw_line
.out:
    popad
    ret

tab_scan:
    mov dword [tab_match_count],0
    mov dword [tab_single_ptr],0
    cmp byte [tab_mode],0
    jne .args

    ; /bin executables 
    mov esi,fs_entries
    mov ecx,FS_COUNT
.fe:
    test ecx,ecx
    jz .ret
    push esi
    push ecx
    call tab_basename
    test eax,eax
    jz .fe_next
    cmp dword [esi+FS_NAME_LEN],2
    jne .fe_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .fe_next
    call tab_hit
.fe_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .fe
.ret:
    ret

    ; argument mode
.args:
    mov esi,fs_entries
    mov ecx,FS_COUNT
.ae:
    test ecx,ecx
    jz .ret2
    push esi
    push ecx
    mov edi,cwd_buf
    call basename_if_child    
    test eax,eax
    jz .ae_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .ae_next
    call tab_hit
.ae_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .ae
.ret2:
    ret

; record/print a match. esi=null-terminated name.
tab_hit:
    inc dword [tab_match_count]
    mov [tab_single_ptr],esi
    cmp byte [tab_printing],0
    je .ret
    push esi
    push eax
.pr:
    mov al,[esi]
    test al,al
    jz .sp
    call putchar
    inc esi
    jmp .pr
.sp:
    mov al,' '
    call putchar
    call putchar
    pop eax
    pop esi
.ret:
    ret

tab_prefix_match:
    push esi
    push edi
    push ecx
    mov edi,[tab_pfx]
    mov ecx,[tab_pfxlen]
    test ecx,ecx
    jz .match             ; empty prefix match all
.cmp_loop:
    mov al,[esi]
    mov ah,[edi]
    cmp al,ah
    jne .no
    test al,al
    jz .match             ; both ended simultaneously
    inc esi
    inc edi
    dec ecx
    jnz .cmp_loop
.match:
    pop ecx
    pop edi
    pop esi
    mov eax,1
    ret
.no:
    pop ecx
    pop edi
    pop esi
    xor eax,eax
    ret

tab_basename:
    push esi
    ; check "/bin/" prefix
    cmp byte [esi],'/'
    jne .no
    cmp byte [esi+1],'b'
    jne .no
    cmp byte [esi+2],'i'
    jne .no
    cmp byte [esi+3],'n'
    jne .no
    cmp byte [esi+4],'/'
    jne .no
    ; make sure there's something after it and no further '/'
    lea esi, [esi+5]
    cmp byte [esi],0
    je .no
    mov eax,esi                  ; name start
.scan:
    mov al,[esi]
    test al,al
    jz .ok
    cmp al,'/'
    je .no                       ; nested dir skip
    inc esi
    jmp .scan
.ok:
    mov eax,[esp]                ; restore esi from stack
    lea eax,[eax+5]              ; /bin/ offset
    pop esi
    ret
.no:
    pop esi
    xor eax,eax
    ret


