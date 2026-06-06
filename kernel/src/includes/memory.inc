; =============================================================================
; memory.inc — physical page allocator and virtual-heap management
; =============================================================================
;
; Two layers, on purpose:
;
;   1. The physical page bitmap tracks ownership of 4 KB frames in the
;      first ~1 GB of physical memory. Bit N is set <=> frame N is in use.
;      The bitmap is 32 KB = 8192 dwords = 262144 bits, which is much more
;      than we need today (the kernel only manages ~16 MB) but is cheap
;      and avoids having to grow it.
;
;   2. The virtual heap is a first-fit allocator that picks a free range
;      inside [heap_start .. heap_end] that does not overlap any entry in
;      alloc_table. Once a range is picked, we allocate physical frames
;      one at a time and map them with map_page.
;
; alloc_table is the bridge: it records (vaddr, page_count) pairs so that
; find_free_virt can avoid collisions, and so that exec_bin can free the
; right range when a userland program returns.
; =============================================================================

%include "hal/hal.inc"

section .text
section .text


; --- Physical page bitmap ---------------------------------------------------
; Each bit tracks one 4 KB frame. Bit (dword_idx * 32 + bit_idx) of the
; bitmap corresponds to physical frame number (dword_idx * 32 + bit_idx),
;-- which is at byte address (frame * 4096).
;
; Bit N corresponds to physical address N * 4096.

; -----------------------------------------------------------------------------
; set_page_used — mark a single page as allocated
;   in:  eax = page-aligned physical address
; -----------------------------------------------------------------------------
set_page_used:
    push ebx
    push ecx
    shr eax, 12                          ; page number
    mov ebx, eax
    shr ebx, 5                           ; dword index in bitmap
    mov ecx, eax
    and ecx, 31                          ; bit index within dword
    bts dword [page_bitmap + ebx * 4], ecx
    pop ecx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; set_page_free — mark a single page as free
;   in:  eax = page-aligned physical address
; -----------------------------------------------------------------------------
set_page_free:
    push ebx
    push ecx
    shr eax, 12
    mov ebx, eax
    shr ebx, 5
    mov ecx, eax
    and ecx, 31
    btr dword [page_bitmap + ebx * 4], ecx
    pop ecx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; alloc_page — find and reserve a free physical frame
;   out: eax = physical frame address (page-aligned), CF = 0
;   out: CF = 1 if no free frames
;
; Linear scan over dwords; the first dword with a clear bit gives us the
; page. Within that dword we do a second linear scan to find the bit.
; This is O(bits-used) in the worst case, which is fine for ~4096 frames.
; For larger pools we'd want a free-list head per dword.
; -----------------------------------------------------------------------------
alloc_page:
    push ebx
    push ecx
    push edx

    xor ebx, ebx                         ; current dword index
.scan_dword:
    cmp ebx, 8192                        ; past end of bitmap?
    jae .fail
    mov eax, [page_bitmap + ebx * 4]
    cmp eax, 0xFFFFFFFF                  ; all bits set?
    jne .found_space
    inc ebx
    jmp .scan_dword
.found_space:
    xor ecx, ecx                         ; bit index
.scan_bit:
    bt eax, ecx
    jnc .free_bit
    inc ecx
    cmp ecx, 32
    jne .scan_bit
    inc ebx
    jmp .scan_dword
.free_bit:
    bts eax, ecx                         ; mark bit as used
    mov [page_bitmap + ebx * 4], eax
    mov eax, ebx
    shl eax, 5
    add eax, ecx
    shl eax, 12                          ; phys_addr = (dword*32 + bit) * 4096
    clc
    jmp .done
.fail:
    stc
.done:
    pop edx
    pop ecx
    pop ebx
    ret


; --- Virtual heap (first-fit) -----------------------------------------------

; -----------------------------------------------------------------------------
; find_free_virt — first-fit search for a contiguous virtual range
;   in:  ecx = page count (> 0)
;   out: eax = virtual base address, CF = 0 success / CF = 1 no space
;
; We start at heap_start and walk forward by one page at a time, checking
; each candidate against every entry in alloc_table. We use 64-bit-ish
; arithmetic by relying on the small range: candidate end = base + pages*4096
; and we compare two disjoint ranges by checking each endpoint ordering.
; -----------------------------------------------------------------------------
find_free_virt:
    push ebx
    push edx
    push esi
    push edi
    mov ebx, heap_start                  ; current candidate base
.try_base:
    mov eax, ecx
    shl eax, 12
    add eax, ebx                         ; eax = candidate end
    cmp eax, heap_end
    ja .fail                             ; would exceed heap limit
    mov edx, eax                         ; edx = our_end (preserved across check)

    mov esi, alloc_table
    mov edi, alloc_table_count
.check:
    mov eax, [esi]
    test eax, eax
    jz .next_entry                       ; empty slot, skip
    push eax                             ; save entry.base
    mov eax, [esi + 4]
    shl eax, 12
    add eax, [esi]                       ; eax = entry_end
    cmp ebx, eax
    pop eax                              ; eax = entry.base (restored)
    jae .next_entry                      ; candidate.base >= entry.end → no overlap
    cmp eax, edx
    jae .next_entry                      ; entry.base >= candidate.end → no overlap
    add ebx, PAGE_SIZE                   ; overlap → advance candidate by one page
    jmp .try_base
.next_entry:
    add esi, 8
    dec edi
    jnz .check

    mov eax, ebx                         ; found a fit
    clc
    pop edi
    pop esi
    pop edx
    pop ebx
    ret
.fail:
    stc
    pop edi
    pop esi
    pop edx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; calc_free_heap — sum the unallocated bytes in the heap
;   out: eax = free bytes
;
; Free bytes = total heap size - sum(page_count) for non-empty alloc_table
; entries. We do this on demand rather than maintaining a running counter
; so that the allocator has no "decrement on free" path to forget.
; -----------------------------------------------------------------------------
calc_free_heap:
    push ebx
    push ecx
    push edi
    xor eax, eax
    mov edi, alloc_table
    mov ecx, alloc_table_count
.sum:
    add eax, [edi + 4]                   ; sum page counts
    add edi, 8
    loop .sum
    shl eax, 12                          ; pages → bytes
    mov ebx, eax
    mov eax, heap_end - heap_start
    sub eax, ebx
    pop edi
    pop ecx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; register_allocation — record a (vaddr, page_count) pair in alloc_table
;   in:  eax = base, ebx = page count
;
; The first empty slot wins. If the table is full, the allocation is
; effectively a leak (the virtual range is still usable, but
; find_free_virt and free_pages won't know about it). That should not
; happen in practice — the shell has 128 slots.
; -----------------------------------------------------------------------------
register_allocation:
    push ecx
    push edi
    mov ecx, alloc_table_count
    mov edi, alloc_table
.find:
    cmp dword [edi], 0
    je .found
    add edi, 8
    loop .find
    jmp .done
.found:
    mov [edi], eax
    mov [edi + 4], ebx
.done:
    pop edi
    pop ecx
    ret


; -----------------------------------------------------------------------------
; free_pages — unmap and release a range of pages
;   in:  eax = base vaddr, ebx = page count
;
; For each page: look up the PTE, release the physical frame back to the
; bitmap, clear the PTE, and flush the TLB entry. We re-derive the PTE
; address from the page-table layout (PD index → page table base, then
; PT index → PTE offset within that table).
; -----------------------------------------------------------------------------
free_pages:
    push ecx
    push edx
    push edi
    mov ecx, ebx
.loop:
    test ecx, ecx
    jz .done
    push eax
    call .get_pte                        ; edi = PTE address (or 0 if unmapped)
    test edi, edi
    jz .next
    mov edx, [edi]
    test edx, 1                          ; present bit
    jz .clear
    and edx, 0xFFFFF000                  ; mask off flags, keep phys addr
    push eax
    mov eax, edx
    call set_page_free                   ; release the physical frame
    pop eax
.clear:
    mov dword [edi], 0                   ; clear the PTE
    HAL_INVLPG eax                         ; flush TLB
.next:
    pop eax
    add eax, PAGE_SIZE
    dec ecx
    jmp .loop
.done:
    pop edi
    pop edx
    pop ecx
    ret

; -----------------------------------------------------------------------------
; get_pte — given a virtual address in eax, return edi = PTE address
;   out: edi = PTE address, or 0 if vaddr not mapped / outside windows
; -----------------------------------------------------------------------------
.get_pte:
    push eax
    push edx
    mov edx, eax
    shr edx, 22                          ; PD index (top 10 bits)

    cmp edx, 768
    je .pte_pt0
    cmp edx, 769
    je .pte_pt1
    cmp edx, 770
    je .pte_pt2
    cmp edx, 771
    je .pte_pt3
    cmp edx, 772
    je .pte_pt4
    xor edi, edi                         ; not in any window
    jmp .pte_fail
.pte_pt0:
    mov edi, identity_page_table
    jmp .pte_calc
.pte_pt1:
    mov edi, kernel_low_page_table
    jmp .pte_calc
.pte_pt2:
    mov edi, heap_page_table_0
    jmp .pte_calc
.pte_pt3:
    mov edi, heap_page_table_1
    jmp .pte_calc
.pte_pt4:
    mov edi, heap_page_table_2
.pte_calc:
    mov edx, eax
    shr edx, 12
    and edx, 0x03FF                      ; PTE index
    lea edi, [edi + edx * 4]             ; edi = PTE address
.pte_fail:
    pop edx
    pop eax
    ret
