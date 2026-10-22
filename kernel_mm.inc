find_free_virt:
    push ebx
    push edx
    push esi
    push edi
    
    mov ebx,heap_start     

.restart_search:
    ; Calculate proposed end address would be
    mov eax,ecx              ; ecx = requested page count
    shl eax,12               ; Convert pages to bytes
    add eax,ebx              ; eax = proposed end
    
    cmp eax, heap_end
    ja .fail                  ; Out of memory!
    mov edx,eax              ; edx = proposed end

    ; EVERY single slot in the table
    mov esi,alloc_table
    mov edi,alloc_table_count

.check_loop:
    mov eax,[esi]            ; Get the base address of this table entry
    test eax,eax
    jz .next_entry           ; If it's 0, the slot is empty, skip it

    push ecx              
    mov ecx,[esi+4]        
    shl ecx,12               ; Convert pages to bytes
    add ecx,eax              ; ecx = entry_end (base + size)

    ; base <= ebx < entry_end
    cmp ebx, eax
    jb .check_condition_2
    cmp ebx, ecx
    jb .overlap_found_pop

.check_condition_2:
    cmp eax, ebx
    jb .no_overlap
    cmp eax, edx
    jb .overlap_found_pop

.no_overlap:
    pop ecx                 

.next_entry:
    add esi,8             
    dec edi
    jnz .check_loop      

    ; THE ENTIRE TABLE IS CLEAN!
    mov eax,ebx              
    clc                       ; Success
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

.overlap_found_pop:
    pop ecx                  
    add ebx, 4096             ; Advance by 1 page
    jmp .restart_search       ; Start clean scan of the whole table

.fail:
    stc                       ; Failure
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

;---------------------------------------------------
; out: 
;   eax =
;  (heap_end-heap_start)-sum(alloc_table pages)*4096
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

;---------------------------
; in:
;   eax = base
;   ebx = page count
;---------------------------
; who borrowed what memory
;---------------------------
register_allocation:
    push ecx
    push edi
    mov ecx,alloc_table_count
    mov edi,alloc_table
.find:
    cmp dword [edi],0
    je .found
    add edi,8
    loop .find
    jmp .done
.found:
    mov [edi],eax
    mov [edi+4],ebx
.done:
    pop edi
    pop ecx
    ret

;-----------------------
; in:
;   eax = base
;   ebx = page count
;-----------------------
free_pages:
    push ecx
    push edx
    push edi
    mov ecx,ebx
.loop:
    test ecx,ecx
    jz .done
    push eax
    call map_page
    jc .next
    mov edx,[edi]
    test edx,1
    jz .clear
    and edx,0FFFFF000h
    push eax
    mov eax,edx
    call set_page_free
    pop eax
.clear:
    mov dword [edi],0
    invlpg [eax]
.next:
    pop eax
    add eax,4096
    dec ecx
    jmp .loop
.done:
    pop edi
    pop edx
    pop ecx
    ret


;-------------------
; in:             
;   esi: -> string
; out: 
;   eax: integer    
;-------------------
set_page_used:
    push ebx
    push ecx
    shr eax,12
    mov ebx,eax
    shr ebx,5        ;dword index
    mov ecx,eax
    and ecx,31       ;bit inside dword
    bts dword [page_bitmap+ebx*4],ecx
    pop ecx
    pop ebx
    ret

;------------------------------
; in:
;   eax = physical page address
;-----------------------------
set_page_free:
    push ebx
    push ecx
    shr eax,12
    mov ebx,eax
    shr ebx,5
    mov ecx,eax
    and ecx,31
    btr dword [page_bitmap+ebx*4],ecx
    pop ecx
    pop ebx
    ret

;------------------------------
; out:
;   eax = page address
;   CF=0 success CF=1 fail
;
;  8192 dword = 32768 bytes
;  each bit tracks one 4 KB page
;  8192*32 bits = 262144 pages
;  262144*4096  = 1 GB 
;------------------------------
;-- Another page in my diary --
;--   Yazoo 6502 memories    --
;------------------------------
alloc_page:
    push ebx
    push ecx
    push edx

    xor ebx,ebx
.scan_dword:
    cmp ebx,8192          ; 8192 dwords=1GB bitmap
    jae .fail
    mov eax,[page_bitmap+ebx*4]
    cmp eax,0FFFFFFFFh
    jne .found_space
    inc ebx
    jmp .scan_dword
.found_space:
    xor ecx,ecx
.scan_bit:
    bt eax,ecx
    jnc .free_bit
    inc ecx
    cmp ecx,32
    jne .scan_bit
    inc ebx
    jmp .scan_dword
.free_bit:
    bts eax,ecx
    mov [page_bitmap+ebx*4],eax
    mov eax,ebx
    shl eax,5
    add eax,ecx
    shl eax,12
    clc
    jmp .done
.fail:
    stc
.done:
    pop edx
    pop ecx
    pop ebx
    ret

;------------------------------
; in:
;   eax = virtual
;   ebx = physical
;   ecx = flags
;-----------------------------
; Creating the illusion that 
;    memory is contiguous
;-----------------------------
map_page:
    push edx
    push edi
    mov edx,eax
    shr edx,22

    cmp edx,768
    je .pt0
    cmp edx,769
    je .pt1
    cmp edx,770
    je .pt2
    cmp edx,771
    je .pt3
    cmp edx,772
    je .pt4

    stc
    jmp .fail

.pt0:
    mov edi,identity_page_table   
    jmp .map
.pt1:
    mov edi,kernel_low_page_table
    jmp .map
.pt2:
    mov edi,heap_page_table_0
    jmp .map
.pt3:
    mov edi,heap_page_table_1
    jmp .map
.pt4:
    mov edi,heap_page_table_2

.map:
    ; Get the PTE 
    mov edx, eax
    shr edx, 12
    and edx, 0x03FF        
    or ebx, ecx
    mov [edi+edx*4],ebx
    invlpg [eax]        ; if CPU had cached 
                        ; wipe it!
    clc                          
.fail:
    pop edi
    pop edx
    ret

sys_heap_info:
    test ebx,ebx
    je .err
    push ecx
    push edx
    push esi
    push edi
    call calc_free_heap              ; eax = free bytes
    mov [ebx],eax
    mov esi,alloc_table
    mov ecx,alloc_table_count
    xor edx,edx                      ; active-entry count
    lea edi,[ebx+8]
.loop:
    mov eax,[esi]
    test eax,eax
    jz .next
    mov [edi],eax                    ; record this base address
    add edi,4
    inc edx
.next:
    add esi,8
    dec ecx
    jnz .loop
    mov [ebx+4],edx
    mov eax,edx
    pop edi
    pop esi
    pop edx
    pop ecx
    ret
.err:
    mov eax,-1
    ret

page_fault_isr:
    cli
    pushad
    mov eax,[esp+36]    ; 36=EIP ADDRESS  32=error
    push eax                
    call page_fault_handler
    add esp,4              
.Tom_Dooley:
    hlt
    jmp .Tom_Dooley

page_fault_handler:
    push ebp
    mov ebp,esp
    mov esi,pf_msg
    call print_cr    ;CR2
    mov esi,pf_addr
    call print
    ;mov eax,cr2
    call print_hex_dword
    call newline
    
    mov edx,0x3333       
.outer:
    mov ecx,0xFFFF    
.inner:
    dec ecx
    jnz .inner
    dec edx
    jnz .outer

    pop ebp
    iret

sys_alloc:
    mov ecx,[argc]
    cmp ecx,2
    jne .usage
    mov esi,[argv+4]
    call sys_asc2int
    mov eax,edx
    test eax,eax
    jz .usage
    add eax,4095
    shr eax,12
    mov ecx,eax              ; page count
    push ecx
    call find_free_virt      ; eax = vbase, CF=1 on fail
    pop ecx
    jc .failed
    mov edi,eax              ; allocation base
    mov edx,eax              ; running vaddr
.alloc_loop:
    test ecx,ecx
    jz .done_alloc
    push edx
    push ecx
    call alloc_page          ; eax = phys
    pop ecx
    pop edx
    jc .failed
    mov ebx,eax              ; phys
    mov eax,edx              ; virt
    push edx
    push ecx
    mov ecx,3                ; flags = present|rw
    call map_page
    pop ecx
    pop edx
    jc .failed
    add edx,4096
    dec ecx
    jmp .alloc_loop
.done_alloc:
    mov eax,edi
    mov ebx,edx
    sub ebx,edi
    shr ebx,12               ; page count
    call register_allocation
    call newline
    mov esi,alloc_mem
    call print
    mov eax,edi
    call print_hex_dword
    call newline
    ret
.failed:
    mov esi,out_mem
    call print_cr
    ret
.usage:
    mov esi,no_arg
    call print_cr
    ret

;------------------------------
; --- The generous recycler ---
;------------------------------
sys_dealloc:
    mov ecx,[argc]
    cmp ecx,2
    jne .usage
    mov esi,[argv+4]
    call sys_hex2int
    mov esi,alloc_table
    mov ecx,alloc_table_count
.search:
    cmp [esi],eax
    je .found
    add esi,8
    loop .search
    mov esi,bad_free_msg
    call print_cr
    ret
.found:
    mov ebx,[esi+4]
    push eax
    call free_pages
    pop eax
    ; clear allocation entry
    mov dword [esi],0
    mov dword [esi+4],0
    mov esi,free_ok_msg
    call print_cr
    ret
.usage:
    mov esi,free_usage_msg
    call print_cr
    ret

