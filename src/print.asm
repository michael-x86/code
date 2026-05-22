HEAP_BASE equ 0xC0800000
HEAP_MAX  equ 0xC1000000

;----------------------------
; in:
;   eax = physical page addr
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
;   marks page free
; -----------------------------
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

; -----------------------------
; out:
;   eax = physical page address
;   CF=0 success CF=1 fail
;
;  8192 dword = 32768 bytes
;  each bit tracks one 4 KB page
;  8192*32 bits = 262144 pages
;  262144*4096  = 1 GB 
; -----------------------------
alloc_page:
    push ebx
    push ecx
    push edx

    xor ebx,ebx
.scan_dword:
    cmp ebx,8192            ; 8192 dwords = 1GB bitmap
    jae .fail
    mov eax,[page_bitmap + ebx*4]
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
    ; physical address
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

; ------------------------------
; in:
;   eax = virtual
;   ebx = physical
;   ecx = flags
; -----------------------------
; Creating the illusion that 
;    memory is contiguous
; -----------------------------
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
    mov edi,V2P(first_page_table)
    jmp .map
.pt1:
    mov edi,V2P(second_page_table)
    jmp .map
.pt2:
    mov edi,V2P(page_table_0)
    jmp .map
.pt3:
    mov edi,V2P(page_table_1)
    jmp .map
.pt4:
    mov edi,V2P(page_table_2)
.map:
    mov edx,eax
    shr edx,12
    and edx,03FFh
    or ebx,ecx
    mov [edi+edx*4],ebx
    invlpg [eax]
    clc
.fail:
    pop edi
    pop edx
    ret


;------------------------------
; in:
;   esi = address
; out:
;   print 16 bytes 
; -----------------------------
peek_cmd:
    
    mov esi,deadbeef     

    push eax 
    call newline
    mov ecx,16
.loop:
    lodsb
    call print_hex_byte
    mov al,' '
    call putchar
    loop .loop
    call newline
    call newline
    pop eax 
    ret

print_hex_byte:
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

;----------------------------
; in:  
;    eax=unsigned 32-bit 
; out: 
;    decimal digits 
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

show_stack:
    pushad
    call newline
    mov ecx,8
    mov edi,esp
.loop:
    mov eax,[edi]
    call print_hex_dword
    call newline
    add edi,4
    dec ecx
    jnz .loop
    popad
    call newline
    ret

show_regs:
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

; --------------------------------------------------------
; ---  Portal to kernel space via the sacred int 0x80  ---
; --------------------------------------------------------
sys_cmd:
    mov eax,3                ; sys_newline
    int 0x80
    ; peek dword at sys_peek_msg
    mov esi,sys_peek_msg
    mov eax,1
    int 0x80
    mov ebx,sys_peek_msg
    mov eax,10               ; sys_read_mem
    int 0x80
    mov ebx,eax
    mov eax,5
    int 0x80
    mov eax,3
    int 0x80
    ret

;------------------------------------
; out:     
;  addrs for `free`
;------------------------------------
; Real estate agent for malloc street
;------------------------------------
heap_cmd:
    push eax
    push ecx
    push esi
    call newline
    mov esi,alloc_table
    mov ecx,alloc_table_count
.loop:
    mov eax,[esi]
    test eax,eax
    jz .next
    push eax
    mov al,'0'
    call putchar
    mov al,'x'
    call putchar
    pop eax
    call print_hex_dword
    call newline
.next:
    add esi,8
    dec ecx
    jnz .loop

    call calc_free_heap
    call print_int_decimal 
    mov esi,in_bytes
    call print_cr 
    pop esi
    pop ecx
    pop eax
    ret

;------------------------------
;  in:  
;     eax = virtual address
;  out: 
;     eax = physical address
;------------------------------
space_cmd:
    mov eax,0xC0100000   ;->0x00100000
    call newline
    mov esi,virt_mem
    call print
    call print_hex_dword
    call newline
    call virt2phys
    jc  .virt      
    mov esi,real_mem
    call print
    call print_hex_dword
.virt:      
    call newline
    call newline
    ret 
