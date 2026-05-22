;### part 5 ###
; --- The final curtain call    --- 
; --- powering down the theater ---
shutdown:
    cli
    mov dx,0xF4
    mov al,0
    out dx,al
    hlt
    ret

; --- print 8 hex digits at top-right ----
; Counting heartbeats of the system clock
; ----------------------------------------
print_tick:
    pushad
    mov eax,[tick_count]
    mov edi,0xC00B8000 + (0*80+72)*2     ; row 0, col 72
    mov ecx,8
.next:
    mov edx,eax
    shr edx,28
    and dl,0x0F
    add dl,'0'
    cmp dl,'9'
    jbe .store
    add dl,7
.store:
    mov dh,0x07
    mov [edi],dx
    shl eax,4
    add edi,2
    dec ecx
    jnz .next
    popad
    ret

; --- Birthing three parallel universes ---
init_tasks:
    ; task0 
    mov eax,task0_stack_top
    sub eax,4
    mov dword [eax],0x202
    sub eax, 4
    mov dword [eax],0x08
    sub eax, 4
    mov dword [eax],task0_entry
    sub eax,32
    mov edi,eax
    mov ecx,8
    xor ebx,ebx
.clear0:
    mov [edi],ebx
    add edi,4
    loop .clear0
    mov [task0_esp],eax

    ; task1 
    mov eax,task1_stack_top
    sub eax,4
    mov dword [eax],0x202
    sub eax,4
    mov dword [eax],0x08
    sub eax,4
    mov dword [eax],task1_entry
    sub eax,32
    mov edi,eax
    mov ecx,8
    xor ebx,ebx
.clear1:
    mov [edi],ebx
    add edi,4
    loop .clear1
    mov [task1_esp],eax

    ; task2 main
    mov eax,task2_stack_top
    sub eax,4
    mov dword [eax],0x202
    sub eax,4
    mov dword [eax],0x08
    sub eax,4
    mov dword [eax],main_loop  ;  entry point
    sub eax,32
    mov edi,eax
    mov ecx,8
    xor ebx,ebx
.clear2:
    mov [edi],ebx
    add edi,4
    loop .clear2
    mov [task2_esp],eax
    ret

;-----------------------------------------------------
; first-fit scan of [heap_start, heap_end) for a
; contiguous run of pages not overlapping any
; alloc_table entry.
; in:  
;    ecx = page count (>0)
; out: 
;    CF=0, eax = virtual base on success
;    CF=1 on failure
;-----------------------------------------------------
find_free_virt:
    push ebx
    push edx
    push esi
    push edi
    mov ebx,heap_start
.try_base:
    mov eax,ecx
    shl eax,12
    add eax,ebx               ; our_end
    cmp eax,heap_end
    ja .fail
    mov edx,eax               ; edx = our_end
    mov esi,alloc_table
    mov edi,alloc_table_count
.check:
    mov eax,[esi]
    test eax,eax
    jz .next_entry            ; empty slot
    push eax                  ; save entry.base
    mov eax,[esi+4]
    shl eax,12
    add eax,[esi]             ; eax = entry_end
    cmp ebx,eax
    pop eax                   ; eax = entry.base
    jae .next_entry           ; ebx >= entry_end, no overlap
    cmp eax,edx
    jae .next_entry           ; entry.base >= our_end, no overlap
    add ebx,4096              ; overlap → advance candidate
    jmp .try_base
.next_entry:
    add esi,8
    dec edi
    jnz .check
    mov eax,ebx
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

;---------------------------------------------------
; out: 
;    eax =
; (heap_end-heap_start)-sum(alloc_table pages)*4096
;---------------------------------------------------
calc_free_heap:
    push ebx
    push ecx
    push edi
    xor eax,eax
    mov edi,alloc_table
    mov ecx,alloc_table_count
.sum:
    add eax,[edi+4]
    add edi,8
    loop .sum
    shl eax,12
    mov ebx,eax
    mov eax,heap_end-heap_start
    sub eax,ebx
    pop edi
    pop ecx
    pop ebx
    ret
