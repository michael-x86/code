; =============================================================================
; exec.inc — userland binary loader and executor
; =============================================================================
;
; Searches /bin/<cmd> in the filesystem, maps it into a fresh virtual region,
; copies the binary in, calls it, then cleans up on return.
; =============================================================================

; -----------------------------------------------------------------------------
; exec_bin — load and run a userland binary from /bin/<argv[0]>
; -----------------------------------------------------------------------------
exec_bin:
    cmp dword [argc], 0
    je .silent

    ; Ensure superblock metadata is loaded before looking up /bin/<command>.
    call ensure_fs_ready

    ; Build path: "/bin/<command>"
    mov edi, path_buf
    mov esi, bin_prefix
.cp_pref:
    lodsb
    stosb
    test al, al
    jnz .cp_pref
    dec edi                      ; back over null terminator
    mov esi, [argv]
.cp_cmd:
    lodsb
    stosb
    test al, al
    jnz .cp_cmd

    ; Look up in filesystem using new VFS
    mov esi, path_buf
    call fs_lookup_inode
    cmp eax, -1
    je .nf
    mov ebx, eax
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FILE
    jne .nf

    ; Read file content into file_block_buf
    mov ebx, eax
    mov edi, file_block_buf
    mov ecx, BLOCK_SIZE
    xor edx, edx
    call file_read
    cmp eax, -1
    je .nf
    mov esi, file_block_buf
    mov ebx, eax             ; ebx = file size

    ; Compute page count (round up)
    mov ecx, ebx
    add ecx, 4095
    shr ecx, 12

    ; Find a free virtual range
    push esi
    push ebx
    push ecx
    call find_free_virt
    pop ecx
    pop ebx
    pop esi
    jc .oom

    mov [exec_vbase], eax
    mov [exec_pages], ecx

    ; Map and populate pages
    push esi
    push ebx
    mov edi, eax
.map_loop:
    test ecx, ecx
    jz .map_done
    push ecx
    push edi
    call alloc_page
    pop edi
    pop ecx
    jc .oom_mapped
    mov ebx, eax
    mov eax, edi
    push ecx
    push edi
    push esi
    mov ecx, PAGE_PRESENT_RW
    call map_page
    pop esi
    pop edi
    pop ecx
    jc .oom_mapped
    add edi, PAGE_SIZE
    dec ecx
    jmp .map_loop
.map_done:
    pop ecx                      ; byte count
    pop esi                      ; data source

    ; Copy binary into mapped region
    mov edi, [exec_vbase]
    cld
    rep movsb

    ; Register allocation
    mov eax, [exec_vbase]
    mov ebx, [exec_pages]
    call register_allocation

    ; Save process name from argv[0]
    push edi
    push esi
    mov esi, [argv]
    mov edi, exec_name
    mov ecx, 31
.cp_name:
    lodsb
    stosb
    test al, al
    jz .name_done
    dec ecx
    jnz .cp_name
    mov byte [edi], 0
.name_done:
    pop esi
    pop edi

    ; Call the binary
    mov eax, [exec_vbase]
    call eax

    ; Clear process name on return
    push edi
    push ecx
    mov edi, exec_name
    mov ecx, 32
    xor eax, eax
    rep stosb
    pop ecx
    pop edi

    ; Free pages on return
    mov eax, [exec_vbase]
    mov ebx, [exec_pages]
    call free_pages

    ; Clear alloc_table entry
    push eax
    push ecx
    mov ecx, alloc_table_count
    mov edi, alloc_table
    mov eax, [exec_vbase]
.find_slot:
    cmp [edi], eax
    je .clear_slot
    add edi, 8
    loop .find_slot
    jmp .cleanup_done
.clear_slot:
    mov dword [edi], 0
    mov dword [edi + 4], 0
.cleanup_done:
    pop ecx
    pop eax
    ret

.oom_mapped:
    ; Partially mapped — free what we did map
    pop ebx
    pop esi
    mov eax, [exec_vbase]
    mov ebx, [exec_pages]
    sub ebx, ecx
    test ebx, ebx
    jz .oom
    call free_pages
.oom:
    PRINT_STRING_CR out_mem
    ret
.nf:
    PRINT_STRING_CR command_nf_msg
.silent:
    ret
