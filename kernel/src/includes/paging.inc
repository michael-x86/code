; =============================================================================
; paging.inc — page directory / table setup, single-page mapping, TLB flush
; =============================================================================
;
; Paging layout
; -------------
; We set up a higher-half kernel with six 4 MB identity-mapped windows
; and a higher-half mirror at 0xC0000000+:
;
;   PDE 0  / 768  -> identity_page_table   (0x00000000 .. 0x003FFFFF)
;   PDE 1  / 769  -> kernel_low_page_table (0x00400000 .. 0x007FFFFF)
;   PDE 2  / 770  -> heap_page_table_0     (0x00800000 .. 0x00BFFFFF)
;   PDE 3  / 771  -> heap_page_table_1     (0x00C00000 .. 0x00FFFFFF)
;   PDE 4  / 772  -> heap_page_table_2     (0x01000000 .. 0x013FFFFF)
;   PDE 65        -> highmem_page_table    (0x10400000 .. 0x107FFFFF)
;   PDE 514       -> identity_page_table (mirror, 0x80800000..0x80BFFFFF)
;
; This dual identity + higher-half mirror is what lets the boot sequence
; get away with computing a physical base via ebp (the kernel is still
; executing from the bootloader-determined physical address) and then
; flip on paging and continue executing from the higher-half address.
;
; The five page tables are placed in BSS (see bss.inc) and zero-initialized
; by the loader; we fill them here.
;
; Position-independence
; --------------------
; Page_mapping and reserve_kernel_pages run BEFORE paging is enabled, so
; every reference to a label must be ebp-relative (the entry point set ebp
; to the physical base of the kernel image). map_page is called both
; before and after paging is enabled, so it uses ebp too.
; =============================================================================
%include "hal/hal.inc"


%include "hal/hal.inc"


%include "hal/hal.inc"

section .text


; -----------------------------------------------------------------------------
; page_mapping — build the page directory and all six page tables
;
; Called once during kernel startup, before paging is enabled. Fills:
;   - identity_page_table, kernel_low_page_table, heap_page_table_0..2,
;     highmem_page_table
;   - page_directory (wires PDEs 0..4, 65, 514, and 768..772; the rest
;     are zero, which means "not present")
; -----------------------------------------------------------------------------
page_mapping:
    ; --- Zero the page directory -------------------------------------------
    lea edi, [page_directory + ebp]
    xor eax, eax
    mov ecx, PAGE_DIR_ENTRIES
.clear_pd:
    mov [edi], eax
    add edi, 4
    loop .clear_pd

    ; --- kernel_low_page_table: physical 0x00400000..0x007FFFFF -----------
    lea edi, [kernel_low_page_table + ebp]
    mov eax, KERNEL_LOW_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_kernel:
    mov ebx, eax
    or  ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_kernel

    ; --- identity_page_table: physical 0x00000000..0x003FFFFF --------------
    lea edi, [identity_page_table + ebp]
    xor ebx, ebx
    mov ecx, PAGE_TABLE_ENTRIES
.make_identity:
    mov eax, ebx
    or  eax, PAGE_PRESENT_RW
    mov [edi], eax
    add ebx, PAGE_SIZE
    add edi, 4
    loop .make_identity

    ; --- heap_page_table_0: physical 0x00800000..0x00BFFFFF ---------------
    lea edi, [heap_page_table_0 + ebp]
    mov eax, HEAP_REGION_0_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap0:
    mov ebx, eax
    or  ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap0

    ; --- heap_page_table_1: physical 0x00C00000..0x00FFFFFF ---------------
    lea edi, [heap_page_table_1 + ebp]
    mov eax, HEAP_REGION_1_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap1:
    mov ebx, eax
    or  ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap1

    ; --- heap_page_table_2: physical 0x01000000..0x013FFFFF ---------------
    lea edi, [heap_page_table_2 + ebp]
    mov eax, HEAP_REGION_2_PHY
    mov ecx, PAGE_TABLE_ENTRIES
.fill_heap2:
    mov ebx, eax
    or  ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_heap2

    ; --- highmem_page_table: physical 0x10400000..0x107FFFFF --------------
    lea edi, [highmem_page_table + ebp]
    mov eax, 0x10400000
    mov ecx, PAGE_TABLE_ENTRIES
.fill_highmem:
    mov ebx, eax
    or  ebx, PAGE_PRESENT_RW
    mov [edi], ebx
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_highmem

    ; --- Wire PDEs: identity map (low) + higher-half mirror (0xC0000000) --
    lea edx, [page_directory + ebp]

    ; PDE[0] and PDE[768] -> identity_page_table
    lea eax, [identity_page_table + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (0 * 4)], eax
    mov [edx + (768 * 4)], eax

    ; PDE[1] and PDE[769] -> kernel_low_page_table
    lea eax, [kernel_low_page_table + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (1 * 4)], eax
    mov [edx + (769 * 4)], eax

    ; PDE[2] and PDE[770] -> heap_page_table_0
    lea eax, [heap_page_table_0 + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (2 * 4)], eax
    mov [edx + (770 * 4)], eax

    ; PDE[3] and PDE[771] -> heap_page_table_1
    lea eax, [heap_page_table_1 + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (3 * 4)], eax
    mov [edx + (771 * 4)], eax

    ; PDE[4] and PDE[772] -> heap_page_table_2
    lea eax, [heap_page_table_2 + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (4 * 4)], eax
    mov [edx + (772 * 4)], eax

    ; PDE[514] — re-use identity_page_table so the kernel doesn't fault
    ; when the FS init path computes an address in this range.
    ; Virtual 0x80800000..0x80BFFFFF maps to physical 0x00000000..0x003FFFFF.
    ; This puts usable RAM behind the page table so reads won't #PF.
    lea eax, [identity_page_table + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (514 * 4)], eax

    ; PDE[65] — identity-map high memory 0x10400000..0x107FFFFF so the
    ; keyboard map data (and any other allocator-placed data) placed at
    ; ≈260–264 MB by the webulator's memory layout doesn't #PF.
    ; Virtual addresses in VIRTUAL_PDE65_START..VIRTUAL_PDE65_END map to
    ; physical 0x10400000..0x107FFFFF.
    lea eax, [highmem_page_table + ebp]
    or  eax, PAGE_PRESENT_RW
    mov [edx + (65 * 4)], eax
    ret


; -----------------------------------------------------------------------------
; reserve_kernel_pages — mark every page that holds kernel code/data as used
;
; Called from the higher-half entry sequence, after paging is enabled.
; Uses kernel_phys_start_var / kernel_phys_end_var (set in higher_half:).
; The end variable is set to the byte just past the end of the .bss region,
;-- so the loop rounds UP to the next page boundary to capture the last
;-- partial page in use.
; -----------------------------------------------------------------------------
reserve_kernel_pages:
    push eax
    push ebx

    mov eax, [kernel_phys_start_var]
    and eax, 0xFFFFF000                  ; round DOWN to page boundary
    mov ebx, [kernel_phys_end_var]
    add ebx, 0x0FFF
    and ebx, 0xFFFFF000                  ; round UP to page boundary
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
; map_page — install a single 4 KB page mapping
;   in:  eax = virtual address, ebx = physical address, ecx = flags
;   out: CF = 0 on success, CF = 1 on failure (vaddr outside any window)
;
; Used by the heap allocator to map individual pages on demand. We pick
; the right page table from the PDE index of the virtual address. The
; PTE index is the bits 12..21 of the virtual address. We merge the
; physical address with the caller's flags (so the caller's "present + rw"
; bits are preserved) and flush the TLB with invlpg.
; -----------------------------------------------------------------------------
map_page:
    push edx
    push edi
    mov edx, eax
    shr edx, 22                          ; PDE index (top 10 bits of vaddr)

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

    stc                                  ; no matching page table
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
    and edx, 0x03FF                      ; PTE index within the page table
    or  ebx, ecx                         ; merge phys addr with caller's flags
    mov [edi + edx * 4], ebx
    HAL_INVLPG eax                       ; flush stale TLB entry for this vaddr
    clc
.fail:
    pop edi
    pop edx
    ret
