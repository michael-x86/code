; =============================================================================
; paging.inc — page directory / table setup, mapping, and TLB management
; =============================================================================
;
; Sets up a higher-half identity-mapped kernel:
;   - Identity map first 20 MB (PDE 0..4)
;   - Mirror same mapping at 0xC0000000+ (PDE 768..772)
;   - 5 page tables: identity, kernel_low, heap_0, heap_1, heap_2
;
; All routines use ebp-relative addressing (set up at kernel entry).
; =============================================================================

; -----------------------------------------------------------------------------
; page_mapping — build page directory and all page tables
; -----------------------------------------------------------------------------
page_mapping:
    ; Clear page directory (1024 entries, all zero)
    lea edi, [page_directory + ebp]
    xor eax, eax
    mov ecx, PAGE_DIR_ENTRIES
.clear_pd:
    mov [edi], eax
    add edi, 4
    loop .clear_pd

    ; --- kernel_low_page_table: physical 0x00400000..0x007FFFFF ---
    lea edi, [kernel_low_page_table + ebp]
    mov eax, KERNEL_LOW_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_kernel:
    mov ebx, eax
    or ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_kernel

    ; --- identity_page_table: physical 0x00000000..0x003FFFFF ---
    lea edi, [identity_page_table + ebp]
    xor ebx, ebx
    mov ecx, PAGE_TABLE_ENTRIES
.make_identity:
    mov eax, ebx
    or eax, PAGE_PRESENT_RW
    mov [edi], eax
    add ebx, PAGE_SIZE
    add edi, 4
    loop .make_identity

    ; --- heap_page_table_0: physical 0x00800000..0x00BFFFFF ---
    lea edi, [heap_page_table_0 + ebp]
    mov eax, HEAP_REGION_0_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap0:
    mov ebx, eax
    or ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap0

    ; --- heap_page_table_1: physical 0x00C00000..0x00FFFFFF ---
    lea edi, [heap_page_table_1 + ebp]
    mov eax, HEAP_REGION_1_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap1:
    mov ebx, eax
    or ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap1

    ; --- heap_page_table_2: physical 0x01000000..0x013FFFFF ---
    lea edi, [heap_page_table_2 + ebp]
    mov eax, HEAP_REGION_2_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap2:
    mov ebx, eax
    or ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap2

    ; --- Wire PDEs: identity map (low) + higher-half mirror (0xC0000000) ---
    lea edx, [page_directory + ebp]

    ; PDE[0] & PDE[768] -> identity_page_table
    lea eax, [identity_page_table + ebp]
    or eax, PAGE_PRESENT_RW
    mov [edx + (0 * 4)], eax
    mov [edx + (768 * 4)], eax

    ; PDE[1] & PDE[769] -> kernel_low_page_table
    lea eax, [kernel_low_page_table + ebp]
    or eax, PAGE_PRESENT_RW
    mov [edx + (1 * 4)], eax
    mov [edx + (769 * 4)], eax

    ; PDE[2] & PDE[770] -> heap_page_table_0
    lea eax, [heap_page_table_0 + ebp]
    or eax, PAGE_PRESENT_RW
    mov [edx + (2 * 4)], eax
    mov [edx + (770 * 4)], eax

    ; PDE[3] & PDE[771] -> heap_page_table_1
    lea eax, [heap_page_table_1 + ebp]
    or eax, PAGE_PRESENT_RW
    mov [edx + (3 * 4)], eax
    mov [edx + (771 * 4)], eax

    ; PDE[4] & PDE[772] -> heap_page_table_2
    lea eax, [heap_page_table_2 + ebp]
    or eax, PAGE_PRESENT_RW
    mov [edx + (4 * 4)], eax
    mov [edx + (772 * 4)], eax
    ret

; -----------------------------------------------------------------------------
; reserve_kernel_pages — mark kernel image pages as used in the bitmap
;   Reads kernel_phys_start_var / kernel_phys_end_var (set in higher_half).
; -----------------------------------------------------------------------------
reserve_kernel_pages:
    push eax
    push ebx

    mov eax, [kernel_phys_start_var]
    and eax, 0xFFFFF000
    mov ebx, [kernel_phys_end_var]
    add ebx, 0x0FFF
    and ebx, 0xFFFFF000
.reserve_loop:
    cmp eax, ebx
    jae .done
    push eax
    call set_page_used
    pop eax
    add eax, PAGE_SIZE
    jmp .reserve_loop
.done:
    pop ebx
    pop eax
    ret

; -----------------------------------------------------------------------------
; map_page — write a single PTE
;   in:  eax = virtual address, ebx = physical address, ecx = flags
;   out: CF = 0 on success, CF = 1 on failure (bad PDE index)
; -----------------------------------------------------------------------------
map_page:
    push edx
    push edi
    mov edx, eax
    shr edx, 22              ; PDE index

    ; Select the correct page table based on virtual address range
    cmp edx, 768
    je .pt0
    cmp edx, 769
    je .pt1
    cmp edx, 770
    je .pt2
    cmp edx, 771
    je .pt3
    cmp edx, 772
    je .pt4

    stc                      ; no matching page table
    jmp .fail

.pt0:
    mov edi, identity_page_table
    jmp .map
.pt1:
    mov edi, kernel_low_page_table
    jmp .map
.pt2:
    mov edi, heap_page_table_0
    jmp .map
.pt3:
    mov edi, heap_page_table_1
    jmp .map
.pt4:
    mov edi, heap_page_table_2

.map:
    mov edx, eax
    shr edx, 12
    and edx, 0x03FF          ; PTE index within the page table
    or ebx, ecx              ; merge phys addr with flags
    mov [edi + edx * 4], ebx
    invlpg [eax]             ; flush stale TLB entry for this vaddr
    clc
.fail:
    pop edi
    pop edx
    ret
