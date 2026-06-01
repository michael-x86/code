[org 0xC0100000]

CODE_SEG equ 0x08   ; Offset - code seg in GDT
DATA_SEG equ 0x10   ; Offset - data seg in GDT

; I would be happy if you allowed my contact details to remain.
; Best regards,
; Michael
; michael@nordstedt.eu

global start

bits 32

start:
    cli
    mov ax,DATA_SEG
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax

    call .get_pc
.get_pc:
    pop ebp            
    sub ebp,.get_pc   ; nice

    ; --- physical stack ---
    lea esp,[stack_top+ebp] 
    push ebp                 
    pushad
   
    call page_mapping

    lea eax, [page_directory+ebp]
    mov cr3,eax

    ; Enable Paging
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax

    ; Jump to virtual memory
    mov eax,higher_half
    jmp eax

higher_half:
    ; Paging is now active!
    ; We are now in virtual memory space.
    ; (Aren't we living in a virtual reality anyway?)

    mov ebp,[esp+32]    
    lea eax,[start+ebp]
    mov [kernel_phys_start_var],eax
    lea eax,[page_bitmap+32768+ebp]
    mov [kernel_phys_end_var],eax

    call reserve_kernel_pages

    popad
    add esp,4             
    mov esp,stack_top
    mov ebp,esp
    jmp kernel_main

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

;-------------------------------

kernel_main:
    call build_idt
    call set_irq0
    call set_irq1
    call set_syscall
    lidt [idt_descriptor]
    call pic_remap
    call set_freq       ; 100 hz. 

    mov edi,cwd_buf
    mov ecx,128
    xor eax,eax
    rep stosb
    mov byte [cwd_buf],'/'

    call load_fs_persist
    call cls
    call banner
    mov eax,80*4
    mov [cursor_pos],eax
    call newline
    call prompt

    call init_tasks
    sti 
    mov [task0_esp],esp
    mov esp,[task0_esp]
    mov dword [current_task],0
    popad
    iretd           ; jumps to task0_entry

; task 0
; Zombie that sleeps and waits for glory
; -------------------------------------
task0_entry:
    .loop:
    ;call task0_stuff
    hlt
    jmp .loop

; task 1
; Another Zombie
; -------------------------------------
task1_entry:
    .loop:
    ;call task1_stuff
    hlt
    jmp .loop

; -------------------------------------
;   shell and command processor 
; -------------------------------------
task2_entry:  
    cmp byte [tick_flag],0
    je .skip
    mov byte [tick_flag],0
    call print_tick
.skip:
    call get_key
    test al,al
    jz .done
    cmp al,13
    je .enter
    cmp al,8
    je .del
    cmp al,9
    je .tab
    call putchar
    mov ebx,[cmd_len]
    cmp ebx,63
    jae .done
    mov [cmd_buf+ebx],al
    inc ebx
    mov [cmd_len],ebx
    jmp .done
.enter:
    mov ebx,[cmd_len]         
    mov byte [cmd_buf+ebx],0
    call newline
    call save_history
    call parse_args
    call dispatch_command
    mov dword [cmd_len],0     ;reset buffer
    mov byte [cmd_buf],0
    call prompt
    hlt
    jmp task2_entry
.tab:
    call tab_complete
    xor al,al
    jmp .done
.del:
    cmp dword [cmd_len],0
    jbe .done
    dec dword [cmd_len]
    call delchar
.done:
    hlt
    jmp task2_entry


;---  Parse ARGS  ---
parse_args:
    mov esi,cmd_buf
    xor ecx,ecx
    mov edi,argv
.skip_spaces:
    mov al,[esi]
    cmp al,' '
    jne .check
    inc esi
    jmp .skip_spaces
.check:
    test al,al
    jz .done
    mov [edi],esi
    add edi,4
    inc ecx
.scan:
    mov al,[esi]
    test al,al
    jz .done
    cmp al,' '
    je .term
    inc esi
    jmp .scan
.term:
    mov byte [esi],0
    inc esi
    jmp .skip_spaces
.done:
    mov [argc],ecx    ;out 
    ret

; ---------------------------
; Because forgetting is human 
; remembering is divine
; ---------------------------
save_history:
    pushad
    cmp dword [cmd_len],0
    je .done
    mov eax,[hist_count]
    cmp eax,32
    jb .append
    mov esi,hist_buf+64
    mov edi,hist_buf
    mov ecx,31*64
    rep movsb
    mov dword [hist_count],31
.append:
    mov eax,[hist_count]
    mov ebx,64
    mul ebx
    mov edi,hist_buf
    add edi,eax
    mov esi,cmd_buf
.copy:
    lodsb
    stosb
    test al,al
    jnz .copy
    inc dword [hist_count]
    mov eax,[hist_count]
    mov [hist_index],eax
.done:
    popad
    ret

hist_back:
    pushad
    mov eax,[hist_index]
    test eax,eax
    jz .done              ; already at oldest
    dec eax
    mov [hist_index],eax
    mov ebx,64
    mul ebx
    mov esi,hist_buf
    add esi,eax
    call clear_cmdline
    mov edi,cmd_buf
    xor ecx,ecx
.copy:
    lodsb
    stosb
    test al,al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len],ecx
    mov esi,cmd_buf
.print:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .print
.done:
    popad
    ret

hist_frwd:
    pushad
    mov eax,[hist_index]
    mov ebx,[hist_count]
    cmp eax,ebx
    jae .done         
    inc eax
    mov [hist_index],eax
    cmp eax,ebx
    je .empty_line    
    mov ecx,64
    mul ecx
    mov esi,hist_buf
    add esi,eax
    call clear_cmdline
    mov edi,cmd_buf
    xor ecx,ecx
.copy:
    lodsb
    stosb
    test al,al
    jz .finish
    inc ecx
    jmp .copy
.finish:
    mov [cmd_len],ecx
    mov esi,cmd_buf
.print:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .print
.empty_line:
    call clear_cmdline
    mov dword [cmd_len],0
.done:
    popad
    ret
clear_cmdline:
    pushad
.loop:
    cmp dword [cmd_len],0
    jbe .done
    dec dword [cmd_len]
    call delchar
    jmp .loop
.done:
    popad
    ret
;--------------------------------------


; -------------------------------------
; lookup table dispatcher
; -------------------------------------
dispatch_command:
    pushad
    mov esi,cmd_table
.next_cmd:
    cmp byte [esi],0
    je .not_found      ; no command
    mov ebx,esi        ; table string
    mov eax,[argc]
    test eax,eax
    jz .not_found
    mov edi,[argv]     ; argv[0] = command
.compare:
    mov al,[ebx]
    mov dl,[edi]
    cmp al,dl
    jne .skip_it
    test al,al
    jz .found
    inc ebx
    inc edi
    jmp .compare
.skip_it:
    cmp byte [esi],0
    je .after_name
    inc esi
    jmp .skip_it
.after_name:
    inc esi              ; skip 0
    add esi,4            ; skip dd handler
    jmp .next_cmd
.found:
.find_end:
    cmp byte [esi],0
    je .get_addr
    inc esi
    jmp .find_end
.get_addr:
    inc esi              ; get cmd address 
    mov eax,[esi]
    mov [cmd_exec],eax   
    popad
    mov eax,[cmd_exec]   
    call eax             ; execute command
    ret
.not_found:
    popad
    call exec_bin
    ret

delchar:
    mov ebx,[cursor_pos]
    cmp ebx,[prompt_limit]
    jbe .rt
    dec ebx
    cmp ebx,[prompt_limit]
    jb .rt
    mov edx,0xC00B8000
    lea edi,[edx+ebx*2]
    mov ax,0x0020   
    mov [edi],ax
    mov [cursor_pos],ebx
    call cursor
.rt:
    ret

putchar:
    push edi
    push edx
    push eax
    mov ebx,[cursor_pos]
    mov edx,0xC00B8000
    lea edi,[edx+ebx*2]
    mov ah,0x02         ; color
    mov [edi],ax
    inc ebx
    cmp ebx,80*25
    jb .st
    call scroll
    mov ebx,80*24
.st:
    mov [cursor_pos],ebx
    call cursor
    pop eax
    pop edx
    pop edi
    ret

newline:
    push eax
    push ebx
    push edx
    mov eax,[cursor_pos]
    mov ebx,80
    xor edx,edx
    div ebx
    inc eax
    mul ebx
    mov [cursor_pos],eax
    cmp eax,80*25
    jb .st
    call scroll
    mov dword [cursor_pos],80*24
.st:
    call cursor
    pop edx
    pop ebx
    pop eax
    ret

prompt:
    mov al,'$'   
    call putchar
    mov al,' '
    call putchar
    mov [prompt_limit],ebx    ; ebx from putch  
    ret

cursor:
    push eax
    push edx
    mov dx,0x3D4
    mov al,0x0F
    out dx,al
    mov dx,0x3D5
    mov al,byte[cursor_pos]
    out dx,al
    mov dx,0x3D4
    mov al,0x0E
    out dx,al
    mov dx,0x3D5
    mov al,byte[cursor_pos+1]
    out dx,al
    pop edx
    pop eax
    ret

;----- string,0 ------
; The oldest form of 
; kernel communication
;---------------------
print:
    push eax
.next:
    lodsb
    test al,al
    jz .done
    call putchar
    jmp .next
.done:
    pop eax
    ret

;---- string,13,0 ----
print_cr:
    push eax
.next:
    lodsb
    test al,al
    jz .done
    cmp al,13
    je .newln
    call putchar
    jmp .next
.newln:
    call newline
    jmp .next
.done:
    pop eax
    ret

; --------------------------------------
; Ego boost at boot - look what we built
; --------------------------------------
banner:
    mov esi,sys_msg
    mov edi,0xC00B8000+(2*80+0)*2
    mov ah,0x02
.next:
    lodsb
    test al,al
    jz .done
    stosw
    jmp .next
.done:
    ret

cls:                       
    mov edi,0xC00B8000     ; VGA text memory
    mov ecx,80*25/2        ; 80x25 characters
    mov eax,0x07200720     ; green+space
    rep stosd
    xor eax,eax
    mov [cursor_pos],eax
    call cursor
    ret

scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi,0xC00B8000+160  
    mov edi,0xC00B8000
    mov ecx,80*24       
.lp1:
    mov ax,[esi]
    mov [edi],ax
    add esi,2
    add edi,2
    loop .lp1
    mov ecx,80
    mov ax,0x0720       
.lp2:
    mov [edi],ax
    add edi,2
    loop .lp2
    pop eax
    pop ecx
    pop edi
    pop esi
    ret

; -----------------------------------------
; returns:
;   al = ascii char
;   al = 0 if no key
; -----------------------------------------
get_key:
    mov eax,[kbd_tail]
    cmp eax,[kbd_head]
    je .empty
    mov bl,[kbd_buf+eax]
    inc eax
    and eax,255
    mov [kbd_tail],eax
    mov al,bl
    test al,al
    jz .empty

    ;xor ebx,ebx     ;key code
    ;movsx ebx,al
    ;mov eax,6
    ;int 0x80

    ; -------------------------
    ; arrow up
    ; -------------------------
    cmp al,0x48
    jne .check_down
    call hist_back
    xor al,al
    ret
.check_down:
    ; -------------------------
    ; arrow down
    ; -------------------------
    cmp al,0x50
    jne .translate
    call hist_frwd
    xor al,al
    ret
.translate:
    ;cmp al,0x01          ; breaks vi
    ;je  shutdown         ;escape key
    ; reject invalid scancodes
    cmp al,128
    jae .invalid   ; Work to be done...
    movzx eax,al
    ; -------------------------
    ; choose keymap
    ; -------------------------
    cmp byte [kbd_shift],0
    jne .shifted
.normal:
    mov al,[keymap+eax]
    ret
.shifted:
    mov al,[keymap_shift+eax]
    ret
.empty:
    xor al,al
    ret
.invalid:
    mov al,'?'
    ret

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
print_tick:
    pushad
    mov eax,[tick_count]
    mov edi,0xC00B8000+(0*80+72)*2  
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
    sub eax, 4
    mov dword [eax],task2_entry ; entry point
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
; first-fit scan of [heap_start,heap_end) for a
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

; -----------------------
; in:
;   eax = base
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


; --------------------
; in:               
;   esi -> string   
; out:              
;   edx integer     
; --------------------
asc2int:      
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

; -------------------
; in:             
;   esi: -> string
; out: 
;   eax: integer    
; -------------------
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
;   eax = page address
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
    invlpg [eax]        ; if CPU had cached an older translation
                        ; for this virtual address - wipe it!
    clc                          
.fail:
    pop edi
    pop edx
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

sys_cmd:
    mov eax,3                ; sys_newline
    int 0x80
    mov ebx,sys_cmd
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
;  pointers to malloc street
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

;-----------------------------------------------------
; Persistence layout: FS region starts at LBA 256.
; Each spare slot occupies SECTORS_PER_SLOT sectors.
;   sector 0 — 68-byte fs_entry record, zero-padded
;   sectors 1..2 — FS_CAPACITY (1024) bytes of content
;-----------------------------------------------------
FS_BASE_LBA       equ 256
SECTORS_PER_SLOT  equ 3

; ata_read - PIO read of ecx sectors from LBA eax into [edi]
ata_read:
    pushad
    mov ebx,eax                  ; LBA
    mov ebp,ecx                  ; sector count
    mov dx,0x1F7
.wb:
    in al,dx
    test al,0x80
    jnz .wb
    mov dx,0x1F6
    mov eax,ebx
    shr eax,24
    and al,0x0F
    or  al,0xE0
    out dx,al
    mov dx,0x1F2
    mov eax,ebp
    out dx,al
    mov dx,0x1F3
    mov eax,ebx
    out dx,al
    mov dx,0x1F4
    mov eax,ebx
    shr eax,8
    out dx,al
    mov dx,0x1F5
    mov eax,ebx
    shr eax,16
    out dx,al
    mov dx,0x1F7
    mov al,0x20                  ; READ SECTORS
    out dx,al
    mov ecx,ebp
.nxs:
    mov dx,0x1F7
.wd:
    in al,dx
    test al,0x80
    jnz .wd
    test al,0x08
    jz .wd
    mov dx,0x1F0
    push ecx
    mov ecx,256
    rep insw
    pop ecx
    dec ecx
    jnz .nxs
    popad
    ret

; ata_write - PIO write of ecx sectors to LBA eax from [esi]
ata_write:
    pushad
    mov ebx,eax
    mov ebp,ecx
    mov dx,0x1F7
.wb:
    in al,dx
    test al,0x80
    jnz .wb
    mov dx,0x1F6
    mov eax,ebx
    shr eax,24
    and al,0x0F
    or  al,0xE0
    out dx,al
    mov dx,0x1F2
    mov eax,ebp
    out dx,al
    mov dx,0x1F3
    mov eax,ebx
    out dx,al
    mov dx,0x1F4
    mov eax,ebx
    shr eax,8
    out dx,al
    mov dx,0x1F5
    mov eax,ebx
    shr eax,16
    out dx,al
    mov dx,0x1F7
    mov al,0x30                  ; WRITE SECTORS
    out dx,al
    mov ecx,ebp
.nxs:
    mov dx,0x1F7
.wd:
    in al,dx
    test al,0x80
    jnz .wd
    test al,0x08
    jz .wd
    mov dx,0x1F0
    push ecx
    mov ecx,256
    rep outsw
    pop ecx
    dec ecx
    jnz .nxs
    ; flush cache
    mov dx,0x1F7
    mov al,0xE7
    out dx,al
.wf:
    in al,dx
    test al,0x80
    jnz .wf
    popad
    ret

;-----------------------------------------------------
;   in: 
;      eax = fs_entries entry ptr
;   writes the slot to disk if it's a spare slot; 
;   no-op otherwise.
;-----------------------------------------------------
persist_entry:
    pushad
    ; slot_idx = (eax-fs_entries)/FS_REC_SIZE
    sub eax,fs_entries
    xor edx,edx
    mov ebx,FS_REC_SIZE
    div ebx                       ; eax=slot_idx
    cmp eax,FS_COUNT-FS_SPARE_COUNT
    jb .skip
    sub eax,FS_COUNT-FS_SPARE_COUNT
    mov ebp,eax                  ; ebp=spare_idx

    ; re-derive entry ptr
    add eax,FS_COUNT-FS_SPARE_COUNT
    imul eax,FS_REC_SIZE
    add eax,fs_entries
    mov ebx,eax                  ; ebx = entry ptr

    ; --- build persist_buf (1536 B) ---
    mov esi,ebx
    mov edi,persist_buf
    mov ecx,FS_REC_SIZE
    cld
    rep movsb
    mov ecx,512-FS_REC_SIZE
    xor eax,eax
    rep stosb
    mov esi,[ebx+FS_NAME_LEN+4]
    mov ecx,FS_CAPACITY
    rep movsb

    ; --- ata_write 3 sectors at FS_BASE_LBA + spare_idx*3 ---
    mov eax,ebp
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov esi,persist_buf
    call ata_write
.skip:
    popad
    ret

;-----------------------------------------------------
; load_fs_persist - called at boot. Reads each spare
; slot from disk and restores its entry + content.
;-----------------------------------------------------
load_fs_persist:
    pushad
    xor ebx,ebx
.lp:
    cmp ebx,FS_SPARE_COUNT
    jae .done
    mov eax,ebx
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov edi,persist_buf
    call ata_read

    cmp byte [persist_buf],0
    je .next                      ; empty on-disk slot

    ; entry ptr=fs_entries+(FS_COUNT-FS_SPARE_COUNT+ebx)*FS_REC_SIZE
    mov eax,FS_COUNT
    sub eax,FS_SPARE_COUNT
    add eax,ebx
    imul eax,FS_REC_SIZE
    add eax,fs_entries
    mov edx,eax                  ; edx = entry ptr

    ; preserve the in-memory data ptr that gen_fs assigned
    mov eax,[edx+FS_NAME_LEN+4]
    push eax
    push edx

    ; copy on-disk record into entry (overwriting data ptr too)
    mov esi,persist_buf
    mov edi,edx
    mov ecx,FS_REC_SIZE
    cld
    rep movsb

    pop edx
    pop eax
    mov [edx+FS_NAME_LEN+4],eax  ; restore real data ptr

    ; copy content from persist_buf+512 to entry's data buffer
    mov edi,eax
    mov esi,persist_buf+512
    mov ecx,FS_CAPACITY
    rep movsb
.next:
    inc ebx
    jmp .lp
.done:
    popad
    ret

; --------------------------------
tab_complete:
    pushad
    mov ecx, [cmd_len]
    test ecx, ecx
    jz .out           
    ; ---- first pass: count matches, remember last one ----
    xor edx, edx                  ; edx = match counter
    mov dword [tab_match_count],0
    mov dword [tab_single_ptr],0
    ; --- scan built-in cmd_table ---
    mov esi,cmd_table
.bi_loop:
    cmp byte [esi],0
    je .bi_done
    push esi
    call tab_prefix_match     ; eax=1 match, 0 no match; esi unchanged
    pop esi
    test eax, eax
    jz .bi_next
    inc edx
    mov [tab_single_ptr],esi  
.bi_next:
    ; skip to end of name (find null)
.bi_skip:
    cmp byte [esi],0
    je .bi_skip_end
    inc esi
    jmp .bi_skip
.bi_skip_end:
    inc esi                       ; skip null
    add esi,4                   ; skip dd handler
    jmp .bi_loop
.bi_done:

    ; --- scan fs_entries for executables (type == 2) ---
    mov esi,fs_entries
    mov ecx,FS_COUNT
.fs_loop:
    test ecx,ecx
    jz .fs_done
    ; extract basename from full vpath  "/bin/ls"
    push esi
    push ecx
    call tab_basename            ; eax = ptr to basename, 0 if not /bin/
    test eax,eax
    jz .fs_next
    ; check type==2 (exec)
    cmp dword [esi+FS_NAME_LEN],2
    jne .fs_next
    ; prefix match against basename
    push eax                     ; save basename ptr
    mov esi,eax                  ; tab_prefix_match reads from esi
    call tab_prefix_match
    pop esi                      ; esi = basename ptr (name to display)
    test eax,eax
    jz .fs_next_pop
    inc edx
    mov [tab_single_ptr],esi
    jmp .fs_next_pop
.fs_next_pop:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .fs_loop
.fs_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .fs_loop
.fs_done:

    mov [tab_match_count],edx
    ; ---- decide what to do ----
    test edx,edx
    jz .out                       ; 0 matches do nothing

    cmp edx,1
    je .do_complete               ; 1 match complete in place

    ; ---- multiple matches: list them ----
    call newline
    call tab_print_all_matches
    call prompt
    mov esi,cmd_buf
    mov ecx,[cmd_len]
.reprint:
    test ecx,ecx
    jz .out
    lodsb
    call putchar
    dec ecx
    jmp .reprint

.do_complete:
    ; --- append missing suffix from tab_single_ptr ---
    mov esi,[tab_single_ptr]
    mov ecx,[cmd_len]
    ; skip ecx chars of the matched name (the prefix already typed)
.skip_prefix:
    test ecx,ecx
    jz .append_rest
    cmp byte [esi],0
    je .out                       ; shouldn't happen
    inc esi
    dec ecx
    jmp .skip_prefix
.append_rest:
    ; copy remaining chars of name into cmd_buf and echo them
.copy_rest:
    mov al,[esi]
    test al,al
    jz .add_space
    ; bounds check: cmd_buf is 64 bytes
    mov ebx,[cmd_len]
    cmp ebx,62
    jae .out
    mov [cmd_buf+ebx],al
    inc dword [cmd_len]
    call putchar
    inc esi
    jmp .copy_rest
.add_space:
    mov ebx,[cmd_len]
    cmp ebx,63
    jae .out
    mov al,' '
    mov [cmd_buf+ebx],al
    inc dword [cmd_len]
    call putchar
.out:
    popad
    ret

tab_prefix_match:
    push esi
    push edi
    push ecx
    mov edi,cmd_buf
    mov ecx,[cmd_len]
    test ecx,ecx
    jz .match             ; empty prefix matches everything
.cmp_loop:
    mov al,[esi]
    mov ah,[edi]
    cmp al,ah
    jne .no
    test al,al
    jz .match             ; both ended simultaneously
    inc esi
    inc edi
    dec ecx
    jnz .cmp_loop
.match:
    pop ecx
    pop edi
    pop esi
    mov eax,1
    ret
.no:
    pop ecx
    pop edi
    pop esi
    xor eax,eax
    ret

tab_basename:
    push esi
    ; check "/bin/" prefix
    cmp byte [esi],'/'
    jne .no
    cmp byte [esi+1],'b'
    jne .no
    cmp byte [esi+2],'i'
    jne .no
    cmp byte [esi+3],'n'
    jne .no
    cmp byte [esi+4],'/'
    jne .no
    ; make sure there's something after it and no further '/'
    lea esi, [esi+5]
    cmp byte [esi],0
    je .no
    mov eax,esi                  ; name start
.scan:
    mov al,[esi]
    test al,al
    jz .ok
    cmp al,'/'
    je .no                       ; nested dir skip
    inc esi
    jmp .scan
.ok:
    mov eax,[esp]                ; restore esi from stack
    lea eax,[eax+5]              ; /bin/ offset
    pop esi
    ret
.no:
    pop esi
    xor eax,eax
    ret

; -------------------------------------------------------------
;  Re-scans both sources and prints every matching name,
;  separated by two spaces, then a newline.
; -------------------------------------------------------------
tab_print_all_matches:
    ; --- built-ins ---
    mov esi, cmd_table
.pbi_loop:
    cmp byte [esi],0
    jmp .pbi_done          ;NORD
    je .pbi_done
    push esi
    call tab_prefix_match
    pop esi
    test eax,eax
    jz .pbi_skip
    ; print name
    push esi
.pbi_print:
    lodsb
    test al,al
    jz .pbi_after
    call putchar
    jmp .pbi_print
.pbi_after:
    ; two spaces as separator
    mov al,' '
    call putchar
    call putchar
    pop esi
.pbi_skip:
.pbi_find_end:
    cmp byte [esi],0
    je .pbi_end
    inc esi
    jmp .pbi_find_end
.pbi_end:
    inc esi                       ; skip null
    add esi,4                    ; skip dd
    jmp .pbi_loop
.pbi_done:

    ; --- fs exec entries ---
    mov esi,fs_entries
    mov ecx,FS_COUNT
.pfs_loop:
    test ecx,ecx
    jz .pfs_done
    push esi
    push ecx
    call tab_basename
    test eax,eax
    jz .pfs_next
    cmp dword [esi+FS_NAME_LEN],2
    jne .pfs_next
    push eax
    mov esi, eax
    call tab_prefix_match
    pop esi
    test eax,eax
    jz .pfs_next
    ; print basename
    push esi
.pfs_print:
    lodsb
    test al,al
    jz .pfs_sep
    call putchar
    jmp .pfs_print
.pfs_sep:
    mov al,' '
    call putchar
    call putchar
    pop esi
.pfs_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .pfs_loop
.pfs_done:
    call newline
    ret

; --------------------------------------------
; - Building the interrupt handler phonebook -
; --------------------------------------------
build_idt:
    mov edi,idt_start
    mov ecx,256
.fill:
    mov eax,isr_default
    mov [edi],ax             ; low
    mov word [edi+2],CODE_SEG 
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax,16
    mov [edi+6],ax           ; high
    add edi,8
    loop .fill

    mov eax,page_fault_isr
    mov ebx,14
    mov edi,idt_start
    call set_idt_entry
    ret

set_idt_entry:
    ; eax = handler address
    ; ebx = entry index
    ; edi = idt base

    mov edx, eax
    mov word [edi+ebx*8+0],dx        ; offset low
    mov word [edi+ebx*8+2],CODE_SEG  ; selector
    mov byte [edi+ebx*8+4],0
    mov byte [edi+ebx*8+5],10001110b 
    shr edx,16
    mov word [edi+ebx*8+6],dx        ; offset high
    ret

page_fault_isr:
    cli
    pushad
    mov eax,[esp+36]    ; 36=EIP ADDRESS  32=error
    push eax                
    call page_fault_handler
    add esp,4              
.hang:
    hlt
    jmp .hang

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
    
    mov edx,0x6666       ; outer loop (adjust for time)
.outer:
    mov ecx,0xFFFF        ; inner loop
.inner:
    dec ecx
    jnz .inner
    dec edx
    jnz .outer

    pop ebp
    iret

isr_default:
    pushad
    mov al,0x20
    out 0x20,al
    popad
    iretd

;----  IRQ 0 & 1  ----
set_irq0:
    mov eax,irq0
    mov edx,eax
    mov edi,idt_start+0x20*8
    mov word [edi],ax
    mov word [edi+2],8   
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr edx,16
    mov word [edi+6],dx
    ret

set_irq1:
    mov eax,irq1
    mov edi,idt_start+0x21*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],10001110b
    shr eax,16
    mov word [edi+6],ax
    ret

;----  syscall (int 0x80)  ----
set_syscall:
    mov eax,syscall_isr
    mov edi,idt_start+0x80*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],11101110b   ; present, DPL=3, 32-bit int gate
    shr eax,16
    mov word [edi+6],ax
    ret

; ---------------------------------------------------------
; int 0x80 dispatcher
;   in : eax = syscall #
;        ebx,ecx,edx,esi,edi = args
;   out: eax = return value (-1 on bad #)
;   all other GPRs preserved across the call
; ---------------------------------------------------------
;  The great gatekeeper of ring 0 
; ---------------------------------------------------------
syscall_isr:
    cmp eax,SYSCALL_COUNT
    jae .bad
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push ebp
    call [syscall_table + eax*4]
    pop ebp
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    iretd
.bad:
    mov eax,-1
    iretd

; --- syscall handlers ---
sys_putchar:                 ; ebx = char
    mov eax,ebx
    call putchar
    xor eax,eax
    ret

sys_print:                   ; esi = ptr
    call print
    xor eax,eax
    ret

sys_print_cr:                ; esi = ptr
    call print_cr
    xor eax,eax
    ret

sys_newline:
    call newline
    xor eax,eax
    ret

sys_cls:
    call cls
    xor eax,eax
    ret

sys_print_hex:               ; ebx = value
    mov eax,ebx
    call print_hex_dword
    xor eax,eax
    ret

sys_print_int:               ; ebx = value
    mov eax,ebx
    call print_int_decimal
    xor eax,eax
    ret

sys_get_key:
    call get_key
    movzx eax,al
    ret

sys_get_tick:
    mov eax,[tick_count]
    ret

sys_shutdown:
    call shutdown
    ret

sys_read_mem:                ; ebx = virt addr -> eax = dword at [ebx]
    mov eax,[ebx]
    ret

sys_getcwd:                  ; edi = dst -> eax = 0
    push esi
    mov esi, cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret

sys_chdir:     ; esi = path -> eax = 0 or -1
    push esi
    push edi
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],0   ; must be 0 (dir)
    jne .nf
    mov esi,resolve_buf
    mov edi,cwd_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edi
    pop esi
    mov eax,-1
    ret

sys_list_dir:     ; (ls) ebx=index, edi=dst -> eax = type or -1
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov [tmp_dst],edi
    mov [tmp_left],ebx
    mov esi,fs_entries
    mov ecx,FS_COUNT
.next:
    test ecx,ecx
    jz .nf
    mov edi,cwd_buf
    call basename_if_child   ; eax = basename ptr(esi) or 0
    test eax, eax
    jz .skip
    cmp dword [tmp_left],0
    je .hit
    dec dword [tmp_left]
.skip:
    add esi,FS_REC_SIZE
    dec ecx
    jmp .next
.hit:
    mov edi,[tmp_dst]
    push esi
    mov esi,eax
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    mov eax,[esi+FS_NAME_LEN]
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
.nf:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov eax,-1
    ret

sys_stat:          ; esi=path, edi=dst_info(12 bytes) -> eax=0/-1
    push esi
    push edi
    push ebx
    push edx
    mov ebx,edi              ; save dst_info
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .nf
    mov edx,[eax+FS_NAME_LEN]
    mov [ebx],edx
    mov edx,[eax+FS_NAME_LEN+4]
    mov [ebx+4], edx
    mov edx,[eax+FS_NAME_LEN+8]
    mov [ebx+8],edx
    pop edx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.nf:
    pop edx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_print_n:                 ; esi=ptr, ecx=count -> eax=0
.lp:
    test ecx,ecx
    jz .done
    mov al,[esi]
    inc esi
    dec ecx
    cmp al,0x0A
    je .nl
    cmp al,0x0D
    je .lp                    ; skip CR
    cmp al,0x09
    jne .raw
    mov al,' '
.raw:
    push ecx
    push esi
    call putchar
    pop esi
    pop ecx
    jmp .lp
.nl:
    push ecx
    push esi
    call newline
    pop esi
    pop ecx
    jmp .lp
.done:
    xor eax,eax
    ret

sys_get_arg:         ; ebx = index, edi = dst -> eax = 0 or -1
    cmp ebx,[argc]
    jae .nf
    push esi
    mov esi,[argv+ebx*4]
    test esi,esi
    jz .nf_pop
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    pop esi
    xor eax,eax
    ret
.nf_pop:
    pop esi
.nf:
    mov eax,-1
    ret

sys_create:
    push esi
    push edi
    push ebx
    push ecx
    push edx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err

    mov edi,fs_entries
    mov ecx,FS_COUNT
.scan:
    test ecx,ecx
    jz .err
    cmp byte [edi],0
    je .got
    add edi,FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    ; copy resolved path into slot
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],1   ; type = file
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_write:           ; echo [text] > file
    push esi
    push edi
    push ebx
    push ecx
    mov edi, resolve_buf
    call fs_resolve
    mov esi, resolve_buf
    call fs_lookup
    test eax, eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1     ; must be regular file
    jne .err
    mov ecx,[esp+0]
    cmp ecx,FS_CAPACITY
    ja .err

    mov esi,[esp+4]                 ; caller's src buffer
    mov edi,[eax+FS_NAME_LEN+4]     ; entry's data ptr
    cld
    rep movsb
    mov ecx,[esp+0]
    mov [eax+FS_NAME_LEN+8],ecx     ; update size
    call persist_entry              ; eax = entry ptr still

    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

;   esi=path -> eax=0/-1
;   Marks slot free; keeps the data buffer
;   in place so it can be reused by a future sys_create.
sys_unlink:
    push esi
    push edi
    push ebx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],1
    jne .err
    mov byte  [eax],0                 ; clear path
    mov dword [eax+FS_NAME_LEN],0     ; type = 0 (free)
    mov dword [eax+FS_NAME_LEN+8],0   ; size = 0
    call persist_entry                ; eax = entry ptr
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_mkdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jnz .err                     ; already exists
    mov edi,fs_entries
    mov ecx,FS_COUNT
.scan:
    test ecx,ecx
    jz .err
    cmp byte [edi],0
    je .got
    add edi,FS_REC_SIZE
    dec ecx
    jmp .scan
.got:
    mov ebx,edi
    mov esi,resolve_buf
.cp:
    lodsb
    stosb
    test al,al
    jnz .cp
    mov dword [ebx+FS_NAME_LEN],0   ; type = dir
    mov dword [ebx+FS_NAME_LEN+8],0 ; size = 0
    mov eax,ebx
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_rmdir:
    push esi
    push edi
    push ebx
    push ecx
    mov edi,resolve_buf
    call fs_resolve
    mov esi,resolve_buf
    call fs_lookup
    test eax,eax
    jz .err
    cmp dword [eax+FS_NAME_LEN],0    ; must be type dir
    jne .err
    push eax
    mov edi,resolve_buf              ; use as cwd for check
    mov esi,fs_entries
    mov ecx,FS_COUNT
.chk:
    test ecx,ecx
    jz .empty
    call basename_if_child
    test eax,eax
    jnz .notempty
    add esi,FS_REC_SIZE
    dec ecx
    jmp .chk
.notempty:
    pop eax
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret
.empty:
    pop eax                    ; eax = entry ptr
    mov byte [eax],0
    mov dword [eax+FS_NAME_LEN],0
    call persist_entry
    pop ecx
    pop ebx
    pop edi
    pop esi
    xor eax,eax
    ret
.err:
    pop ecx
    pop ebx
    pop edi
    pop esi
    mov eax,-1
    ret

sys_ps_info:
    cmp ebx,0 ; Basic sanity check
    je .error
    push edx
    mov edx,[current_task]
    mov [ebx+0],edx
    mov edx,[task0_esp]
    mov [ebx+4],edx
    mov edx,[task1_esp]
    mov [ebx+8],edx
    mov edx,[task2_esp]
    mov [ebx+12],edx
    pop edx
    xor eax,eax ; success
    ret
.error:
    mov eax,-1  ; error
    ret

;EDI
;ESI
;EBP
;original ESP
;EBX
;EDX
;ECX
;EAX

sys_stack_dump:
    call show_regs
    pushad
    mov esi,esp
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
    ret

; ---------------------------------------------
;  Memory dealer - handing out 4KB crack rocks
; ---------------------------------------------
sys_alloc:
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

sys_peek:
    push eax 
    push esi
    mov eax,[argc]
    cmp eax,2
    jl .arguse
    mov esi,[argv+4]
    call sys_hex2int   ;in esi {str} out eax {int}
    call newline
    mov esi,eax 
    mov ecx,4
.loop:
    lodsb
    call print_hex_byte
    mov al,' '
    call putchar
.cont:
    loop .loop
    call newline
    call newline
.done:
    pop esi
    pop eax 
    ret
.arguse:
    mov esi,peek_msg
    call print_cr
    pop esi
    pop eax 
    ret


sys_poke:
    push eax 
    push esi
    mov eax,[argc]
    cmp eax,3
    jl .usage
    mov esi,[argv+4]
    call sys_hex2int  
    mov edi,eax
    mov esi,[argv+8]
    call sys_hex2int   
    mov [edi],eax 
.done:
    pop esi
    pop eax 
    ret
.usage:
    mov esi,poke_msg
    call print_cr
    pop esi
    pop eax 
    ret


; tab completion stuff

basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi+1], 0
    je .cwd_root      

    ; non-root cwd — match as exact prefix
.mp:
    mov al, [edi]
    test al,al
    jz .after_cwd
    mov bl, [esi]
    cmp al, bl
    jne .nope
    inc esi
    inc edi
    jmp .mp
.after_cwd:
    cmp byte [esi],'/'
    jne .nope
    inc esi
    cmp byte [esi],0
    je .nope
    mov ebx, esi
    mov edx, esi
.scan:
    mov al,[edx]
    test al,al
    jz .yes
    cmp al,'/'
    je .nope
    inc edx
    jmp .scan
.yes:
    mov eax,ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

.cwd_root:
    cmp byte [esi],'/'
    jne .nope
    cmp byte [esi+1],0
    je .nope                 ; entry is "/" itself
    mov ebx,esi
    inc ebx                  ; basename = past leading '/'
    mov edx,ebx
.sr:
    mov al, [edx]
    test al,al
    jz .root_ok
    cmp al,'/'
    je .nope
    inc edx
    jmp .sr
.root_ok:
    mov eax,ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

.nope:
    xor eax,eax
    pop edi
    pop esi
    pop edx
    pop ebx
    ret

; in:  esi=str1 (null-term), edi=str2 (null-term)
; out: eax = 0 if equal, 1 if not
; preserves esi, edi
str_eq:
    push esi
    push edi
.lp:
    mov al, [esi]
    mov ah, [edi]
    cmp al, ah
    jne .ne
    test al, al
    jz .eq
    inc esi
    inc edi
    jmp .lp
.eq:
    pop edi
    pop esi
    xor eax,eax
    ret
.ne:
    pop edi
    pop esi
    mov eax,1
    ret

; in:  esi = absolute null-terminated path
; out: eax = ptr to fs_entry, or 0
fs_lookup:
    push ecx
    push edi
    mov edi, fs_entries
    mov ecx, FS_COUNT
.lp:
    test ecx,ecx
    jz .nf
    push esi
    push edi
    call str_eq
    pop edi
    pop esi
    test eax,eax
    jz .found
    add edi, FS_REC_SIZE
    dec ecx
    jmp .lp
.found:
    mov eax, edi
    pop edi
    pop ecx
    ret
.nf:
    xor eax,eax
    pop edi
    pop ecx
    ret

; in:  esi = path (may be absolute, ".", "..", or single-name relative)
;      edi = dst buffer (>= 128 bytes)
; out: writes absolute resolved path to dst (always succeeds syntactically)
; preserves esi/edi
fs_resolve:
    push eax
    push ebx
    push esi
    push edi

    cmp byte [esi],'/'
    je .abs

    cmp byte [esi],'.'
    jne .relative
    cmp byte [esi+1],0
    je .dot                
    cmp byte [esi+1],'.'
    jne .relative
    cmp byte [esi+2],0
    jne .relative
    ; ".."
    jmp .dotdot

.dot:
    mov esi,cwd_buf
.abs:
.cp_abs:
    lodsb
    stosb
    test al,al
    jnz .cp_abs
    jmp .done

.dotdot:
    mov ebx, edi             ; dst origin
    mov esi, cwd_buf
.cp_dd:
    lodsb
    stosb
    test al,al
    jnz .cp_dd
    dec edi                  ; on null
.find:
    dec edi
    cmp edi, ebx
    jbe .at_root
    cmp byte [edi],'/'
    jne .find
    cmp edi, ebx
    je .at_root
    mov byte [edi],0
    jmp .done
.at_root:
    mov edi, ebx
    mov byte [edi],'/'
    mov byte [edi+1],0
    jmp .done

.relative:
    mov ebx,esi             ; name ptr
    mov esi,cwd_buf
    cmp byte [esi+1], 0
    je .root_join
.cp_cwd:
    lodsb
    stosb
    test al,al
    jnz .cp_cwd
    mov byte [edi-1],'/'    ; replace terminating null with '/'
    jmp .append
.root_join:
    mov byte [edi],'/'
    inc edi
.append:
    mov esi,ebx
.cp_name:
    lodsb
    stosb
    test al,al
    jnz .cp_name

.done:
    pop edi
    pop esi
    pop ebx
    pop eax
    ret

; ---------------------------------------------------------
;  exec_bin — fallback from devops commands
; ---------------------------------------------------------
exec_bin: 
    cmp dword [argc], 0
    je .silent
    ; --- build path "/bin/<cmd>" ---
    mov edi,path_buf
    mov esi,bin_prefix
.cp_pref:
    lodsb
    stosb
    test al,al
    jnz .cp_pref
    dec edi                         ; back over null
    mov esi,[argv]
.cp_cmd:
    lodsb 
    stosb
    test al,al
    jnz .cp_cmd

    ; --- FS lookup ---
    mov esi,path_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],2   ; type must be 2 (exec)
    jne .nf

    ; --- save data ptr + size ---
    mov esi,[eax+FS_NAME_LEN+4]     ; esi = binary data ptr
    mov ebx,[eax+FS_NAME_LEN+8]     ; ebx = byte count

    ; --- compute page count (round up) ---
    mov ecx,ebx
    add ecx,4095
    shr ecx,12                      ; ecx = page count

    ; --- find a free virtual range ---
    push esi
    push ebx
    push ecx
    call find_free_virt             ; eax = vbase, CF=1 on fail
    pop ecx
    pop ebx
    pop esi
    jc .oom
     
    ; --- save vbase and page count for cleanup ---
    mov [exec_vbase],eax
    mov [exec_pages],ecx

    ; --- map pages one by one ---
    push esi                        ; save data src ptr
    push ebx                        ; save byte count
    mov edi,eax                     ; edi = current virt addr
.map_loop:
    test ecx,ecx
    jz .map_done
    push ecx
    push edi
    call alloc_page                 ; eax = phys frame, CF=1 fail
    pop edi  
    pop ecx  
    jc .oom_mapped
    
    mov ebx,eax                     ; ebx = phys
    mov eax,edi                     ; eax = virt
    push ecx
    push edi
    push esi
    mov ecx,3                       ; flags: present | rw
    call map_page
    pop esi
    pop edi
    pop ecx
    jc .oom_mapped

    add edi,4096
    dec ecx

    jmp .map_loop
.map_done:
    pop ecx                         ; byte count
    pop esi                         ; data src

    ; --- copy binary into mapped region ---
    mov edi,[exec_vbase]
    cld
    rep movsb

    ; --- register in alloc_table ---
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    call register_allocation

    ; --- call binary entry point ---
    mov eax, [exec_vbase]
    call eax
   
    ; --- free pages after return ---
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    call free_pages

    ; --- clear alloc_table entry ---
    push eax
    push ecx
    mov ecx,alloc_table_count
    mov edi,alloc_table
    mov eax,[exec_vbase]
.find_slot:
    cmp [edi],eax
    je .clear_slot
    add edi,8
    loop .find_slot
    jmp .cleanup_done
.clear_slot:
    mov dword [edi],0
    mov dword [edi+4],0
.cleanup_done:
    pop ecx
    pop eax
    ret

.oom_mapped:
    ; partial map: free whatever was mapped, fall through to oom msg
    pop ebx
    pop esi
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    sub ebx,ecx                     ; pages successfully mapped
    test ebx,ebx
    jz .oom
    call free_pages
.oom:
    mov esi,out_mem
    call print_cr
    ret
.nf:
    mov esi,command_nf_msg
    call print_cr
.silent:
    ret

;------------------------------------

irq0:
    pushad
    inc dword [tick_count]
    inc dword [tick_div]
    test dword [tick_div],1
    jnz .no_flag
    mov byte [tick_flag],1
.no_flag:
    mov eax,[current_task]
    inc eax
    cmp eax,3
    jb .save
    xor eax,eax        ; wrap back to 0
.save:
    ; save current esp into correct slot
    cmp dword [current_task], 0
    je .was0
    cmp dword [current_task], 1
    je .was1
    mov [task2_esp], esp
    jmp .load
.was0:
    mov [task0_esp], esp
    jmp .load
.was1:
    mov [task1_esp], esp
.load:
    mov [current_task], eax
    cmp eax, 0
    je .run0
    cmp eax, 1
    je .run1
    mov esp, [task2_esp]    ; main loop
    jmp .done
.run0:
    mov esp, [task0_esp]
    jmp .done
.run1:
    mov esp, [task1_esp]
.done:
    mov al, 0x20
    out 0x20, al
    popad
    iretd

irq1:
    pushad
    in al,0x60
    mov bl,al
    ; -------------------------
    ; left/right shift press
    ; -------------------------
    cmp bl,0x2A
    je .shift_on
    cmp bl,0x36
    je .shift_on
    ; -------------------------
    ; left/right shift release
    ; -------------------------
    cmp bl,0xAA
    je .shift_off
    cmp bl,0xB6
    je .shift_off
    ; -------------------------
    ; ignore all key releases
    ; -------------------------
    test bl,0x80
    jnz .done
    ; -------------------------
    ; store key press
    ; -------------------------
    mov eax,[kbd_head]
    mov [kbd_buf+eax],bl
    inc eax
    and eax,255
    mov [kbd_head],eax
    jmp .done
.shift_on:
    mov byte [kbd_shift],1
    jmp .done
.shift_off:
    mov byte [kbd_shift],0
.done:
    mov al,0x20
    out 0x20,al
    popad
    iretd


; -------------------------------------------
;  Intel 8259A 
;  IRQ lines -> CPU IRQ vectors
; -------------------------------------------
;  Rewiring the interrupt controller's brain
; -------------------------------------------
pic_remap:            
    mov al,0x11
    out 0x20,al
    out 0xA0,al
    mov al,0x20          ; master offset= 0x20
    out 0x21,al
    mov al,0x28          ; slave offset = 0x28
    out 0xA1,al
    mov al,0x04
    out 0x21,al
    mov al,0x02
    out 0xA1,al
    mov al,0x01
    out 0x21,al
    out 0xA1,al
    mov al,0b11111100   
    out 0x21, al
    mov al,0b11111111    ; disable slave completely
    out 0xA1, al
    ret

set_freq:
    ; freq=1193182/divisor
    ; 100 Hz -> divisor=11932
    mov al,0x36
    out 0x43,al
    mov ax,11932
    out 0x40,al          ; low byte
    mov al,ah
    out 0x40,al          ; high byte
    ret

sys_msg   db "*** x86 Operating System (EFBEADDE) ***", 0

deadbeef  db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

eax_lbl db "EAX: ",0
ebx_lbl db "EBX: ",0
ecx_lbl db "ECX: ",0
edx_lbl db "EDX: ",0
esi_lbl db "ESI: ",0
edi_lbl db "EDI: ",0
ebp_lbl db "EBP: ",0
esp_lbl db "ESP: ",0

tick_flag    db 0
tick_count   dd 0      
tick_div     dd 0     
cursor_pos   dd 0 
prompt_limit dd 0

;--- buffers ---
kbd_head     dd 0
kbd_tail     dd 0
; kbd_buf in .bss

;--- cmd ---
out_mem   db "OUT OF MEMORY - KEEP DREAMING",13,0
alloc_mem db "heap pointer  : 0x",0
no_arg    db 13,'usage: alloc <bytes>',13,0
poke_msg  db 13,"usage: poke <hex addr> <hex value>",13,0
peek_msg  db 13,"usage: peek <hex addr>",13,0

in_bytes        db " bytes free",13,0
free_usage_msg  db 13,"usage: free <hex addr>",13,0
free_ok_msg     db "memory released",13,0
bad_free_msg    db "invalid allocation",13,0

pf_msg db 13,"PAGE FAULT",13,0
pf_addr db "ADDRESS: 0x",0

kernel_phys_start_var: dd 0
kernel_phys_end_var:   dd 0

bin_prefix   db "/bin/",0
exec_vbase   dd 0
exec_pages   dd 0

argc        dd 0
argv        times 16 dd 0

cmd_len     dd 0
cmd_exec    dd 0
; cmd_buf in .bss

command_nf_msg   db 13,"command not found",13,0

hist_count  dd 0
hist_index  dd 0
; hist_buf in .bss

;---- INTERRUPT DESC TABLE ----
; idt_start / idt_end in .bss
idt_descriptor:
    dw idt_end-idt_start-1
    dd idt_start

;---- SYSCALL TABLE  (index = eax at int 0x80) ----
syscall_table:
    dd sys_putchar       ; 0 : ebx = char
    dd sys_print         ; 1 : esi = string ptr
    dd sys_print_cr      ; 2 : esi = string ptr (CR aware)
    dd sys_newline       ; 3
    dd sys_cls           ; 4
    dd sys_print_hex     ; 5 : ebx = value
    dd sys_print_int     ; 6 : ebx = value
    dd sys_get_key       ; 7 : -> eax = ascii (0 if none)
    dd sys_get_tick      ; 8 : -> eax = tick_count
    dd sys_shutdown      ; 9
    dd sys_read_mem      ; 10: ebx = addr -> eax = dword at [addr]
    dd sys_getcwd        ; 11: edi = dst
    dd sys_chdir         ; 12: esi = path 
    dd sys_list_dir      ; 13: ebx = idx, edi = dst 
    dd sys_get_arg       ; 14: ebx = idx, edi = dst 
    dd sys_stat          ; 15: esi = path,edi = info(12B) 
    dd sys_print_n       ; 16: esi = ptr, ecx = count 
    dd sys_create        ; 17: esi = path 
    dd sys_write         ; 18: esi = path,ebx = buf,ecx = n
    dd sys_unlink        ; 19: esi = path 
    dd sys_mkdir         ; 20: esi = path  
    dd sys_rmdir         ; 21: esi = path 
    dd sys_ps_info       ; 22: ebx = dst ptr 
    dd sys_stack_dump    ; 23: 
    dd sys_alloc         ; 24: in = <bytes> -> page ptr 
    dd sys_dealloc       ; 25: in = <page ptr> 
    dd sys_peek          ; 26: in = <address> 
    dd sys_poke          ; 27: in = <address> <value> 
    dd sys_hex2int       ; 28: in = edi->string out=eax
SYSCALL_COUNT equ ($-syscall_table)/4

;---- Keycode -> ASCII Convertion ----
kbd_shift db 0

section .rodata
; -----------------------------------------
; normal keymap
; -----------------------------------------

keymap:
    db 0,27,'1','2','3','4','5','6','7','8'
    db '9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i'
    db 'o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k'
    db 'l',';',39,'`',0,'\'
    db 'z','x','c','v','b','n','m'
    db ',','.','/',0,'*',0,' '
    times 0x3B-($-keymap) db 0     ; F1
    db '<'
    times 256-($-keymap) db 0


; -----------------------------------------
; shift keymap
; -----------------------------------------

keymap_shift:
    db 0,27,'!','@','#','$','%','^','&','*'
    db '(' ,')','_','+',8,9
    db 'Q','W','E','R','T','Y','U','I'
    db 'O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K'
    db 'L',':','"', '~',0,'|'
    db 'Z','X','C','V','B','N','M'
    db '<','>','?',0,'*',0,' '
    times 0x3B-($-keymap_shift) db 0     ; F1
    db '<'
    times 256-($-keymap_shift) db 0

; --------------------------------------------------
; format: db "command",0 
;         dd address 
; final   db 0
; --------------------------------------------------
cmd_table:             ; BBox Cmds
    db "heap",0
    dd heap_cmd
    db 0          ; end of BuzyBox

;---- in-kernel virtual filesystem ----
%include "fs.inc"

section .bss
;----------------------------
alignb 16

tab_match_count  resd 1
tab_single_ptr   resd 1

task0_esp    resd 1
task1_esp    resd 1
task2_esp    resd 1
current_task resd 1

alignb 4
kbd_buf      resb 256
cmd_buf      resb 64
hist_buf     resb 32*64
dir_buf      resb 512

;---- VFS state ----
cwd_buf      resb 128
resolve_buf  resb 128
path_buf     resb 128
tmp_dst      resd 1
tmp_left     resd 1
persist_buf  resb 1536    ; 3 sectors: metadata + 1024 B content

alignb 8
idt_start:
    resb 256*8
idt_end:

alignb 16
task0_stack:
    resb 4096
task0_stack_top:

alignb 16
task1_stack:
    resb 4096
task1_stack_top:

alignb 16
task2_stack:
    resb 4096
task2_stack_top:

;-------------------------------

;---- STACK ----
alignb 16
stack_bottom:
    resb 16384      ; 16 KB stack
stack_top:

alignb 4096
page_directory:
    resd 1024

alignb 4096
identity_page_table:
    resd 1024

alignb 4096
kernel_low_page_table: 
    resd 1024

alignb 4096
heap_page_table_0:
    resd 1024

alignb 4096
heap_page_table_1:
    resd 1024

alignb 4096
heap_page_table_2:
    resd 1024

alignb 4
heap_start:
    resb 12*1024*1024    
heap_end:

alignb 4
page_bitmap:
    resb 32768

alloc_table_count equ 128

alloc_table:
; entry format:
; +0  virtual address
; +4  page count

resd alloc_table_count*2
