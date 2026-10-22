sys_getcwd:                  ; edi = dst -> eax = 0
    push esi
    mov esi, cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret

sys_chdir:     ; esi = path -> eax = 0 or -1
    push esi
    push edi
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],0   ; must be 0 (dir)
    jne .nf
    mov esi,resolve_buf
    mov edi,cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edi
    pop esi
    mov eax,-1
    ret

sys_list_dir:     ; (ls) ebx=index, edi=dst -> eax = type or -1
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov [tmp_dst],edi
    mov [tmp_left],ebx
    mov esi,fs_entries
    mov ecx,FS_COUNT
.next:
    test ecx,ecx
    jz .nf
    mov edi,cwd_buf
    call basename_if_child   ; eax = basename ptr(esi) or 0
    test eax, eax
    jz .skip
    cmp dword [tmp_left],0
    je .hit
    dec dword [tmp_left]
.skip:
    add esi,FS_REC_SIZE
    dec ecx
    jmp .next
.hit:
    mov edi,[tmp_dst]
    push esi
    mov esi,eax
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    mov eax,[esi+FS_NAME_LEN]
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.nf:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov eax,-1
    ret

sys_stat:          ; esi=path, edi=dst_info(12 bytes) -> eax=0/-1
    push esi
    push edi
    push ebx
    push edx
    mov ebx,edi              ; save dst_info
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    mov edx,[eax+FS_NAME_LEN]
    mov [ebx],edx
    mov edx,[eax+FS_NAME_LEN+4]
    mov [ebx+4], edx
    mov edx,[eax+FS_NAME_LEN+8]
    mov [ebx+8],edx
    pop edx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

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

sys_write:           ; echo [text] > file
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
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
    call persist_entry              ; eax = entry ptr still

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

;  esi=path, ebx=buf, ecx=n -> append n bytes to an existing file.
sys_append:
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1     ; must be a regular file
    jne .err
    mov ebx,[eax+FS_NAME_LEN+8]       ; current size = append offset
    mov ecx,[esp+0]                   ; n
    mov edx,ebx
    add edx,ecx
    cmp edx,FS_CAPACITY               ; size + n must fit
    ja .err
    mov esi,[esp+4]                   ; src buffer
    mov edi,[eax+FS_NAME_LEN+4]       ; data ptr
    add edi,ebx                       ; + size
    cld
    rep movsb
    mov ecx,[esp+0]
    add ecx,ebx                       ; new size = old size+n
    mov [eax+FS_NAME_LEN+8],ecx
    call persist_entry
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

;   esi=path -> eax=0/-1
;   Marks slot free
;   so it can be reused by a future sys_create.
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
    mov byte  [eax],0               
    mov dword [eax+FS_NAME_LEN],0     
    mov dword [eax+FS_NAME_LEN+8],0  
    call persist_entry             
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

sys_mkdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err                     ; already exists
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
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],0   ; type = dir
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
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

sys_rmdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],0    ; must be type dir
    jne .err
    push eax
    mov edi,resolve_buf              ; use as cwd for check
    mov esi,fs_entries
    mov ecx,FS_COUNT
.chk:
    test ecx,ecx
    jz .empty
    call basename_if_child
    test eax,eax
    jnz .notempty
    add esi,FS_REC_SIZE
    dec ecx
    jmp .chk
.notempty:
    pop eax
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret
.empty:
    pop eax                    ; eax = entry ptr
    mov byte [eax],0
    mov dword [eax+FS_NAME_LEN],0
    call persist_entry
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

basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi+1],0
    je .cwd_root      
.mp:
    mov al, [edi]
    test al,al
    jz .after_cwd
    mov bl, [esi]
    cmp al, bl
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
    mov ebx, esi
    mov edx, esi
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
    mov al, [edx]
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

; ------------------------------
; in:  
;    esi edi = stings (0 term.)
; out: 
;    eax = 0 if equal, 1 if not
; ------------------------------
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
    xor eax,eax
    ret
.ne:
    pop edi
    pop esi
    mov eax,1
    ret

fs_lookup:
    push ecx
    push edi
    mov edi, fs_entries
    mov ecx, FS_COUNT
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
    add edi, FS_REC_SIZE
    dec ecx
    jmp .lp
.found:
    mov eax, edi
    pop edi
    pop ecx
    ret
.nf:
    xor eax,eax
    pop edi
    pop ecx
    ret

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
    mov ebx, edi             ; dst origin
    mov esi, cwd_buf
.cp_dd:
    lodsb
    stosb
    test al,al
    jnz .cp_dd
    dec edi                  ; on null
.find:
    dec edi
    cmp edi, ebx
    jbe .at_root
    cmp byte [edi],'/'
    jne .find
    cmp edi, ebx
    je .at_root
    mov byte [edi],0
    jmp .done
.at_root:
    mov edi, ebx
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

; ----- spawn process -----
