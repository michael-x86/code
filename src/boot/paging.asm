; -----------------------------------------
; page directory entries
; -----------------------------------------
page_mapping:
    lea edi,[page_directory+ebp]
    xor eax,eax
    mov ecx,1024
.clear_pd:
    mov [edi],eax
    add edi,4
    loop .clear_pd

; -----------------------------------------
; kernel_low_page_table
; physical 0x00400000 - 0x007FFFFF
; -----------------------------------------
    lea edi,[kernel_low_page_table+ebp]
    mov eax,0x00400000
    mov ecx,1024
.fill_kernel:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,0x1000
    add edi,4
    loop .fill_kernel

; ------------------------------------------
; identity map first 4MB
; ------------------------------------------
    lea edi,[identity_page_table+ebp]
    xor ebx,ebx
    mov ecx,1024
.make_identity:
    mov eax,ebx
    or eax,3
    mov [edi],eax
    add ebx,4096
    add edi,4
    loop .make_identity

; ------------------------------------------
; heap_page_table_0
; physical 0x00800000-0x00BFFFFF
; ------------------------------------------
    lea edi,[heap_page_table_0+ebp]
    mov eax,0x00800000
    mov ecx,1024
.fill_heap0:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap0

; ------------------------------------------
; heap_page_table_1
; physical 0x00C00000-0x00FFFFFF
; ------------------------------------------
    lea edi,[heap_page_table_1+ebp]
    mov eax,0x00C00000
    mov ecx,1024
.fill_heap1:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap1

; ------------------------------------------
; heap_page_table_2
; physical 0x01000000-0x013FFFFF
; ------------------------------------------
    lea edi,[heap_page_table_2+ebp]
    mov eax,0x01000000
    mov ecx,1024
.fill_heap2:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap2

; -------------------------------------------
; Map PDEs 
; -------------------------------------------
    lea edx, [page_directory+ebp]

    ; PDE[0] & PDE[768] -> identity_page_table
    lea eax, [identity_page_table + ebp]
    or eax,3
    mov [edx+(0*4)],eax
    mov [edx+(768*4)],eax

    ; PDE[1] & PDE[769] -> kernel_low_page_table
    lea eax, [kernel_low_page_table + ebp]
    or eax,3
    mov [edx+(1*4)],eax
    mov [edx+(769*4)],eax

    ; PDE[2] & PDE[770] -> heap_page_table_0
    lea eax, [heap_page_table_0 + ebp]
    or eax,3
    mov [edx+(2*4)],eax
    mov [edx+(770*4)],eax

    ; PDE[3] & PDE[771] -> heap_page_table_1
    lea eax, [heap_page_table_1 + ebp]
    or eax,3
    mov [edx+(3*4)],eax
    mov [edx+(771*4)],eax

    ; PDE[4] & PDE[772] -> heap_page_table_2
    lea eax, [heap_page_table_2 + ebp]
    or eax,3
    mov [edx+(4*4)],eax
    mov [edx+(772*4)],eax
    ret

; ----------------------------------------------------
; Saving our own soul from the page allocator's greed
; ----------------------------------------------------
reserve_kernel_pages:
    push eax
    push ebx
    
    mov eax, [kernel_phys_start_var]
    and eax, 0FFFFF000h
    mov ebx, [kernel_phys_end_var]
    add ebx, 0FFFh
    and ebx, 0FFFFF000h
.reserve_loop:
    cmp eax,ebx
    jae .done
    push eax
    call set_page_used
    pop eax
    add eax,4096
    jmp .reserve_loop
.done:
    pop ebx
    pop eax
    ret
