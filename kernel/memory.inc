; =============================================================================
; memory.inc — physical page allocator, heap management, and page bitmap
; =============================================================================
;
; Physical allocator: 1-bit-per-page bitmap (32 KB = 1 GB addressable).
; Heap: first-fit virtual address search + alloc_table registration.
; =============================================================================

; ── Physical page bitmap ────────────────────────────────────────────────────
; Each bit tracks one 4 KB page.  8192 dwords × 32 bits = 262144 pages = 1 GB.

; -----------------------------------------------------------------------------
; set_page_used — mark a single page as allocated
;   in: eax = page-aligned physical address
; -----------------------------------------------------------------------------
set_page_used:
    push ebx
    push ecx
    shr eax, 12              ; page number
    mov ebx, eax
    shr ebx, 5               ; dword index in bitmap
    mov ecx, eax
    and ecx, 31              ; bit index within dword
    bts dword [page_bitmap + ebx * 4], ecx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; set_page_free — mark a single page as free
;   in: eax = page-aligned physical address
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
; alloc_page — find and allocate a free physical page
;   out: eax = physical page address, CF = 0 success / CF = 1 out of memory
; -----------------------------------------------------------------------------
alloc_page:
    push ebx
    push ecx
    push edx

    xor ebx, ebx             ; current dword index
.scan_dword:
    cmp ebx, 8192            ; past end of bitmap?
    jae .fail
    mov eax, [page_bitmap + ebx * 4]
    cmp eax, 0xFFFFFFFF      ; all bits set?
    jne .found_space
    inc ebx
    jmp .scan_dword
.found_space:
    xor ecx, ecx             ; bit index
.scan_bit:
    bt eax, ecx
    jnc .free_bit
    inc ecx
    cmp ecx, 32
    jne .scan_bit
    inc ebx
    jmp .scan_dword
.free_bit:
    bts eax, ecx             ; mark bit as used
    mov [page_bitmap + ebx * 4], eax
    ; phys_addr = (dword_idx * 32 + bit_idx) * 4096
    mov eax, ebx
    shl eax, 5
    add eax, ecx
    shl eax, 12
    clc
    jmp .done
.fail:
    stc
.done:
    pop edx
    pop ecx
    pop ebx
    ret

; ── Virtual heap (first-fit) ────────────────────────────────────────────────

; -----------------------------------------------------------------------------
; find_free_virt — first-fit search for a contiguous virtual address range
;   in:  ecx = page count (> 0)
;   out: eax = virtual base address, CF = 0 success / CF = 1 no space
; -----------------------------------------------------------------------------
find_free_virt:
    push ebx
    push edx
    push esi
    push edi
    mov ebx, heap_start      ; current candidate base
.try_base:
    ; end = base + pages * 4096
    mov eax, ecx
    shl eax, 12
    add eax, ebx
    cmp eax, heap_end
    ja .fail                 ; exceeds heap limit
    mov edx, eax             ; edx = our_end

    ; Check against every occupied alloc_table entry
    mov esi, alloc_table
    mov edi, alloc_table_count
.check:
    mov eax, [esi]
    test eax, eax
    jz .next_entry           ; empty slot, skip
    push eax
    mov eax, [esi + 4]
    shl eax, 12
    add eax, [esi]           ; eax = entry_end
    cmp ebx, eax
    pop eax                  ; eax = entry.base
    jae .next_entry          ; base >= entry_end → no overlap
    cmp eax, edx
    jae .next_entry          ; entry.base >= our_end → no overlap
    add ebx, PAGE_SIZE       ; overlap → advance candidate by one page
    jmp .try_base
.next_entry:
    add esi, 8
    dec edi
    jnz .check

    mov eax, ebx             ; found a fit
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
; calc_free_heap — sum unallocated bytes in the heap
;   out: eax = free bytes
; -----------------------------------------------------------------------------
calc_free_heap:
    push ebx
    push ecx
    push edi
    xor eax, eax
    mov edi, alloc_table
    mov ecx, alloc_table_count
.sum:
    add eax, [edi + 4]       ; sum page counts
    add edi, 8
    loop .sum
    shl eax, 12              ; pages → bytes
    mov ebx, eax
    mov eax, heap_end - heap_start
    sub eax, ebx
    pop edi
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; register_allocation — record a virtual allocation in alloc_table
;   in: eax = base, ebx = page count
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
; free_pages — unmap and free a range of pages
;   in: eax = base, ebx = page count
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
    call map_page            ; get PTE pointer into edi
    jc .next
    mov edx, [edi]
    test edx, 1              ; is page present?
    jz .clear
    and edx, 0xFFFFF000
    push eax
    mov eax, edx
    call set_page_free       ; release physical frame
    pop eax
.clear:
    mov dword [edi], 0       ; clear PTE
    invlpg [eax]             ; flush TLB
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
