; =============================================================================
; exec.inc — userland binary loader and executor
; =============================================================================
;
; exec_bin takes the first argument (argv[0]) and runs /bin/<argv[0]> as
; if it were a function: we look up the file in the VFS, map it into a
; freshly-allocated virtual range, call it, and on `ret` we unmap and
; free everything.
;
; The "function call" model is the userland ABI: the binary starts with
; a `call .get_pc; pop ebp; sub ebp, .get_pc` so it can address its own
; data via `lea reg, [ebp + label]`, and it returns with `ret`. The
; loader's job is to put the binary in executable, readable, writable
; memory and jump to it.
;
; Size: binaries may span multiple pages. We size the mapping from the
; inode and read the file straight into the mapped region with
; file_read (which is multi-block aware), so there is no 4 KB ceiling.
; The build script enforces a generous upper bound at compile time.
; =============================================================================


section .text

; --- Program metadata offsets (ABI contract) ---
PROG_NAME_OFFSET    equ 16      ; offset from program base where program_name starts
PROG_VERSION_OFFSET equ 32      ; offset from program base where program_version starts

; -----------------------------------------------------------------------------
; display_prog_metadata — extract and display program name/version in banner
;
; Reads program_name and program_version from the loaded binary and
; displays them in row 0 after the system banner.
; Format: " | name version"
; -----------------------------------------------------------------------------
display_prog_metadata:
    pushad

    mov edi, VGA_TEXT_BUFFER + 20 * 2

    ; Write " | " (black-on-green)
    mov ax, (VGA_COLOR_BANNER << 8) | ' '
    stosw
    mov ax, (VGA_COLOR_BANNER << 8) | '|'
    stosw
    mov ax, (VGA_COLOR_BANNER << 8) | ' '
    stosw

    ; Write program_name (black-on-green)
    mov eax, [exec_vbase]
    add eax, PROG_NAME_OFFSET
    mov esi, eax
.copy_name:
    lodsb
    test al, al
    jz .name_done
    mov ah, VGA_COLOR_BANNER
    stosw
    jmp .copy_name
.name_done:

    ; Write "v" (black-on-green)
    mov ax, (VGA_COLOR_BANNER << 8) | 'v'
    stosw

    ; Write program_version (black-on-green)
    mov eax, [exec_vbase]
    add eax, PROG_VERSION_OFFSET
    mov esi, eax
.copy_version:
    lodsb
    test al, al
    jz .version_done
    mov ah, VGA_COLOR_BANNER
    stosw
    jmp .copy_version
.version_done:

    popad
    ret

; -----------------------------------------------------------------------------
; exec_bin — load and run /bin/<argv[0]>
;
; Silent return on argc == 0 (nothing to do). Prints a "command not
; found" message if /bin/<argv[0]> doesn't exist. Maps the binary
; read-only after the copy (we don't yet enforce this in the PTE; the
; mapping is RW for now). Frees the allocation and clears the
; alloc_table slot when the binary returns.
;
; Before calling the program, extracts program_name and program_version
; from the binary header and displays them in the top banner row.
; -----------------------------------------------------------------------------
exec_bin:
    cmp dword [argc], 0
    je .silent

    ; The superblock might not be loaded yet on the first command of
    ; the session, so make sure the FS metadata is in memory before
    ; we touch it.
    call ensure_fs_ready

    ; --- Build the absolute path "/bin/<argv[0]>" ------------------------
    mov edi, path_buf
    mov esi, bin_prefix
.cp_pref:
    lodsb
    stosb
    test al, al
    jnz .cp_pref
    dec edi                              ; back over the NUL
    mov esi, [argv]
.cp_cmd:
    lodsb
    stosb
    test al, al
    jnz .cp_cmd

    ; --- Look up the binary in the VFS -----------------------------------
    mov esi, path_buf
    call fs_lookup_inode
    cmp eax, -1
    je .nf
    mov ebx, eax
    call load_inode
    cmp dword [inode_buf + INOFS_TYPE], INODE_FILE
    jne .nf

    ; --- Determine the file size from its inode --------------------------
    ; eax = inode number (preserved across load_inode). We stash it so the
    ; binary can be read straight into the mapped region after we map it,
    ; which lets a binary span many pages instead of one block.
    mov [exec_inode], eax
    mov ecx, [inode_buf + INOFS_SIZE]    ; ecx = file size in bytes
    test ecx, ecx
    jz .nf                               ; empty file: nothing to run

    ; --- Find a free virtual range ---------------------------------------
    add ecx, 4095
    shr ecx, 12                          ; round up to whole pages
    call find_free_virt                  ; eax = vbase (ecx preserved)
    jc .oom

    mov [exec_vbase], eax
    mov [exec_pages], ecx

    ; --- Allocate and map one page at a time ----------------------------
    mov edi, eax                         ; edi = running virtual address
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
    mov ecx, PAGE_PRESENT_RW
    call map_page
    pop edi
    pop ecx
    jc .oom_mapped
    add edi, PAGE_SIZE
    dec ecx
    jmp .map_loop
.map_done:

    ; --- Read the binary straight into the mapped region -----------------
    mov ebx, [exec_inode]                ; ebx = inode number
    mov edi, [exec_vbase]
    mov ecx, [inode_buf + INOFS_SIZE]    ; ecx = byte count
    xor edx, edx                         ; offset 0
    call file_read                       ; ebx=inode, edi=dest, ecx=count
    cmp eax, -1
    je .nf

    mov eax, [exec_vbase]                 ; register_allocation expects
    mov ebx, [exec_pages]                 ; eax = base, ebx = page count
    call register_allocation

    ; --- Stash argv[0] in exec_name for diagnostics ---------------------
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

    ; --- Extract and display program metadata ----------------------------
    ;call display_prog_metadata

    ; --- Call the binary -------------------------------------------------
    mov eax, [exec_vbase]
    call eax

    ; --- Restore original banner ----------------------------------------
    call banner

    ; --- Cleanup on return ----------------------------------------------
    push edi
    push ecx
    mov edi, exec_name
    mov ecx, 32
    xor eax, eax
    rep stosb
    pop ecx
    pop edi

    mov eax, [exec_vbase]
    mov ebx, [exec_pages]
    call free_pages

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
    ; We partially mapped before the failure — free what we did map.
    ; The stack is balanced at every jump site here, so nothing to pop.
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


section .rodata

bin_prefix       db "/bin/", 0
command_nf_msg   db 13, "command not found", 13, 0
