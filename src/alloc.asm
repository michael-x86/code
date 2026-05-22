;### part 6 ###
help_cmd:
    push esi
    mov esi,help_lbl
    call print_cr
    pop esi
    ret  

; ---------------------------------------------
;  Memory dealer - handing out 4KB crack rocks
; ---------------------------------------------
alloc_cmd:
    mov ecx,[argc]
    cmp ecx,2
    jne .usage
    mov esi,[argv+4]
    call asc2int
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
free_cmd:
    mov ecx,[argc]
    cmp ecx,2
    jne .usage
    mov esi,[argv+4]
    call hex2int
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

;---------------------------
; in:
;   eax = virtual base
;   ebx = page count
;---------------------------
; Secretary keeping track of 
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

; ------------------------------
; in:
;   eax = virtual address
; out:
;   edi = PTE address
;   CF=0 success CF=1 fail
; ------------------------------
get_pte_ptr:
    push ebx
    push edx
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
    mov edi,V2P(first_page_table)
    jmp .walk
.pt1:
    mov edi,V2P(second_page_table)
    jmp .walk
.pt2:
    mov edi,V2P(page_table_0)
    jmp .walk
.pt3:
    mov edi,V2P(page_table_1)
    jmp .walk
.pt4:
    mov edi,V2P(page_table_2)
.walk:
    mov ebx,eax
    shr ebx,12
    and ebx,03FFh
    lea edi,[edi+ebx*4]
    clc
.fail:
    pop edx
    pop ebx
    ret

; -----------------------
; in:
;   eax = virtual base
;   ebx = page count
; -----------------------
free_pages:
    push ecx
    push edx
    push edi
    mov ecx,ebx
.loop:
    test ecx,ecx
    jz .done
    push eax
    call get_pte_ptr
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
