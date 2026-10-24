;-----------------------------------------------------
; scan of [heap_start,heap_end) for a
; contiguous run of pages not overlapping any
; alloc_table entry.
; in:  
;   ecx = page count (>0)
; out: 
;   CF=0, eax = virtual base on success
;   CF=1 on failure
;-----------------------------------------------------
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
sys_hex2int:
    xor eax,eax
    xor ebx,ebx
    mov bl,[esi]
    cmp bl,'0'
    jne .parse
    cmp byte [esi+1],'x'
    je .skip_prefix
    cmp byte [esi+1],'X'
    jne .parse
.skip_prefix:
    add esi,2
.parse:
    movzx ebx,byte [esi]
    test bl,bl
    jz .done_hex
    cmp bl,'0'
    jb .done_hex
    cmp bl,'9'
    jle .digit
    cmp bl,'A'
    jb .lower
    cmp bl,'F'
    jle .upper
    cmp bl,'a'
    jb .done_hex
    cmp bl,'f'
    ja .done_hex
.lower:
    sub bl,'a'-10
    jmp .shift
.upper:
    sub bl,'A'-10
    jmp .shift
.digit:
    sub bl,'0'
.shift:
    shl eax,4
    or eax,ebx
    inc esi
    jmp .parse
.done_hex:
    ret

   
;--------------------
; in:            
;   esi -> string 
; out:            
;   edx integer          
;--------------------   
sys_asc2int:             
    xor edx, edx
.loop: 
    movzx eax,byte[esi]
    test al,al
    jz .done
    cmp al,' '
    je .done
    sub eax,'0'
    imul edx,edx,10
    add edx,eax      
    inc esi
    jmp .loop
.done:
    ret

;----------------------------
; in:
;   eax = page addr
;   marks page allocated
;----------------------------
;     Marking territory - 
; "this page belongs to us"
;----------------------------
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

sys_hex_byte:   
    push eax
    shr al,4
    and al,0x0F
    call print_hex_nibble
    mov al,byte [esp]
    and al,0x0F
    call print_hex_nibble
    pop eax
    ret

print_hex_nibble:
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    ret

print_hex_dword:
    push eax
    push ecx
    mov ecx,8
.hloop:
    rol dword [esp+4],4
    mov al,byte [esp+4]
    and al,0x0F
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

sys_out_hex:
    push eax
    push ecx
    mov ecx,8
.hloop:
    rol dword [esp+4],4
    mov al,byte [esp+4]
    and al,0x0F
    cmp al,9
    jbe .num
    add al,55
    jmp .out
.num:
    add al,'0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret

sys_mem_dump:
    pushad
    call newline
    mov ecx,64        ; total bytes
    mov edx,16        ; bytes per line
.loop:
    lodsb            
    call sys_hex_byte
    mov al,32  
    call putchar
    dec edx
    jnz .skip_nl
    call newline
    mov edx,16
.skip_nl:
    loop .loop
    popad
    ret

;----------------------------
; in:  
;  eax=unsigned 32-bit 
;----------------------------      
print_int_decimal:
    push eax
    push ebx
    push ecx
    push edx
    mov ecx,0
    mov ebx,10
    test eax,eax
    jnz .divide
    mov al,'0'    ; special case: zero
    call putchar
    jmp .fin
.divide:
    xor edx,edx
    div ebx       ; eax = quotient, edx = remainder
    push edx      ; save digit (0-9)
    inc ecx
    test eax,eax
    jnz .divide
.emit:
    pop edx
    mov al,dl
    add al,'0'
    call putchar
    loop .emit
.fin:
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret


sys_reg_dump:
    pushad
    mov ebp,esp
    call newline
    mov esi,eax_lbl
    call print
    mov eax,[ebp+28]
    call print_hex_dword
    call newline
    mov esi,ecx_lbl
    call print
    mov eax,[ebp+24]
    call print_hex_dword
    call newline
    mov esi,edx_lbl
    call print
    mov eax,[ebp+20]
    call print_hex_dword
    call newline
    mov esi,ebx_lbl
    call print
    mov eax,[ebp+16]
    call print_hex_dword
    call newline
    mov esi,esp_lbl
    call print
    mov eax,[ebp+12]
    call print_hex_dword
    call newline
    mov esi,ebp_lbl
    call print
    mov eax,[ebp+8]
    call print_hex_dword
    call newline
    mov esi,esi_lbl
    call print
    mov eax,[ebp+4]
    call print_hex_dword
    call newline
    mov esi,edi_lbl
    call print
    mov eax,[ebp+0]
    call print_hex_dword
    call newline
    call newline
    popad
    ret

echo_cmd:
    mov eax,[argc]
    cmp eax,2
    jl .done
    mov esi,[argv+4]
.print:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .print
.done:
    call newline
    ret

;------------------------------------
; out:     
;  pointers to malloc street
;------------------------------------
; Real estate agent for malloc street
;------------------------------------
;   snapshot heap allocation table for heap.
;   ebx = dst buffer (needs 8 + alloc_table_count*4 bytes)
;   [dst+0]        = free heap bytes
;   [dst+4]        = number of active allocations (n)
;   [dst+8 + i*4]  = base address of each active allocation (i = 0..n-1)
;   returns eax = n, or -1 if ebx is null
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

print_dec:
    push eax
    push ebx
    push ecx
    push edx
    push edi
    lea edi,[dec_buf+10]
    mov byte [edi],0
    mov ebx,10
.loop:
    dec edi
    xor edx,edx
    div ebx
    add dl,'0'
    mov [edi],dl
    test eax,eax
    jnz .loop
.print:
    mov al,[edi]
    test al,al
    jz .done
    movzx ebx,al
    mov eax,39      ; sys_putchar
    int 0x80
    inc edi
    jmp .print
.done:

    pop edi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret


