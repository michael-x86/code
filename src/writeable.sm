; ---------------------------------------------------------
;  Writeable FS syscalls
; ---------------------------------------------------------

;   esi=path -> eax=0/-1
;   resolves path against cwd, refuses if it already exists,
;   finds the first free slot (path[0]==0) and assigns it.
;   Buffer pointer is pre-baked by gen_fs.py.
sys_create:
    push esi
    push edi
    push ebx
    push ecx
    push edx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err

    ; scan fs_entries for first empty path
    mov edi,fs_entries
    mov ecx,FS_COUNT
.scan:
    test ecx,ecx
    jz .err
    cmp byte [edi],0
    je .got
    add edi,FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    ; copy resolved path into slot
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],1   ; type = file
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

;   esi=path, ebx=src buf, ecx=count -> eax=0/-1
;   overwrites file content; size = count.
;   refuses if path is a dir/exec or count > FS_CAPACITY.
sys_write:
    push esi
    push edi
    push ebx
    push ecx
    ; esp+0=ecx esp+4=ebx esp+8=edi esp+12=esi
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1     ; must be regular file
    jne .err
    mov ecx,[esp+0]
    cmp ecx,FS_CAPACITY
    ja .err

    mov esi,[esp+4]                 ; caller's src buffer
    mov edi,[eax+FS_NAME_LEN+4]     ; entry's data ptr
    cld
    rep movsb
    mov ecx,[esp+0]
    mov [eax+FS_NAME_LEN+8],ecx     ; update size
    call persist_entry                    ; eax = entry ptr still

    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

; sys_unlink — esi=path -> eax=0/-1
;   only regular files (type=1). Marks slot free; keeps the data buffer
;   in place so it can be reused by a future sys_create.
sys_unlink:
    push esi
    push edi
    push ebx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1
    jne .err
    mov byte  [eax],0                 ; clear path
    mov dword [eax+FS_NAME_LEN],0     ; type = 0 (free)
    mov dword [eax+FS_NAME_LEN+8],0   ; size = 0
    call persist_entry                ; eax = entry ptr
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

; -------------------------------------------------
; in:  
;    esi=entry_path, edi=cwd
; out: 
;    eax = pointer to basename inside entry_path, 
;      or 0 if not a direct child
basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi+1],0
    je .cwd_root        ;NORD

    ; non-root cwd — match as exact prefix
.mp:
    mov al,[edi]
    test al,al
    jz .after_cwd
    mov bl,[esi]
    cmp al,bl
    jne .nope
    inc esi
    inc edi
    jmp .mp
.after_cwd:
    cmp byte [esi],'/'
    jne .nope
    inc esi
    cmp byte [esi],0
    je .nope
    mov ebx,esi
    mov edx,esi
.scan:
    mov al,[edx]
    test al,al
    jz .yes
    cmp al,'/'
    je .nope
    inc edx
    jmp .scan
.yes:
    mov eax,ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

.cwd_root:
    cmp byte [esi],'/'
    jne .nope
    cmp byte [esi+1],0
    je .nope                 ; entry is "/" itself
    mov ebx,esi
    inc ebx                  ; basename = past leading '/'
    mov edx,ebx
.sr:
    mov al,[edx]
    test al,al
    jz .root_ok
    cmp al,'/'
    je .nope
    inc edx
    jmp .sr
.root_ok:
    mov eax,ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

.nope:
    xor eax,eax
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

; in:  
;   esi=str1 (null-term), edi=str2 (null-term)
; out: 
;   eax = 0 if equal, 1 if not
str_eq:
    push esi
    push edi
.lp:
    mov al,[esi]
    mov ah,[edi]
    cmp al,ah
    jne .ne
    test al,al
    jz .eq
    inc esi
    inc edi
    jmp .lp
.eq:
    pop edi
    pop esi
    xor eax,eax
    ret
.ne:
    pop edi
    pop esi
    mov eax,1
    ret

; in:  
;   esi = absolute null-terminated path
; out: 
;   eax = ptr to fs_entry, or 0
fs_lookup:
    push ecx
    push edi
    mov edi,fs_entries
    mov ecx,FS_COUNT
.lp:
    test ecx,ecx
    jz .nf
    push esi
    push edi
    call str_eq
    pop edi
    pop esi
    test eax,eax
    jz .found
    add edi,FS_REC_SIZE
    dec ecx
    jmp .lp
.found:
    mov eax,edi
    pop edi
    pop ecx
    ret
.nf:
    xor eax,eax
    pop edi
    pop ecx
    ret

; in:  
;   esi = path 
;   edi = dst buffer (>= 128 bytes)
; out: 
;   writes absolute resolved path to dst 
fs_resolve:
    push eax
    push ebx
    push esi
    push edi

    cmp byte [esi],'/'
    je .abs

    cmp byte [esi],'.'
    jne .relative
    cmp byte [esi+1],0
    je .dot                
    cmp byte [esi+1],'.'
    jne .relative
    cmp byte [esi+2],0
    jne .relative
    ; ".."
    jmp .dotdot

.dot:
    mov esi,cwd_buf
.abs:
.cp_abs:
    lodsb
    stosb
    test al,al
    jnz .cp_abs
    jmp .done

.dotdot:
    mov ebx,edi             ; dst origin
    mov esi,cwd_buf
.cp_dd:
    lodsb
    stosb
    test al,al
    jnz .cp_dd
    dec edi                  ; on null
.find:
    dec edi
    cmp edi,ebx
    jbe .at_root
    cmp byte [edi],'/'
    jne .find
    cmp edi,ebx
    je .at_root
    mov byte [edi],0
    jmp .done
.at_root:
    mov edi,ebx
    mov byte [edi],'/'
    mov byte [edi+1],0
    jmp .done

.relative:
    mov ebx,esi             ; name ptr
    mov esi,cwd_buf
    cmp byte [esi+1], 0
    je .root_join
.cp_cwd:
    lodsb
    stosb
    test al,al
    jnz .cp_cwd
    mov byte [edi-1],'/'    ; replace terminating null with '/'
    jmp .append
.root_join:
    mov byte [edi],'/'
    inc edi
.append:
    mov esi,ebx
.cp_name:
    lodsb
    stosb
    test al,al
    jnz .cp_name

.done:
    pop edi
    pop esi
    pop ebx
    pop eax
    ret

; ---------------------------------------------------------
;  exec_bin — fallback for unknown commands
;  builds /bin/<argv0>, looks up in fs_entries, copies the
;  blob to PROG_LOAD_ADDR, calls it.
; ---------------------------------------------------------
exec_bin:
    cmp dword [argc],0
    je .silent               ; empty line — say nothing
    mov edi,path_buf
    mov esi,bin_prefix
.cp_pref:
    lodsb
    stosb
    test al,al
    jnz .cp_pref
    dec edi                  ; back over trailing null
    mov esi,[argv]
.cp_cmd:
    lodsb
    stosb
    test al,al
    jnz .cp_cmd

    mov esi,path_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],2
    jne .nf

    mov esi,[eax+FS_NAME_LEN+4]
    mov ecx,[eax+FS_NAME_LEN+8]
    mov edi,PROG_LOAD_ADDR
    cld
    rep movsb

    call PROG_LOAD_ADDR
    ret
.nf:
    mov esi,cmd_nf_msg
    call print_cr
.silent:
    ret
