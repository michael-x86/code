[org 0xC0100000]

global start

bits 32

MAX_PROC equ 8        ; process-table: slots 0-2 base tasks, 3+ spawned
PROC_NAME_LEN equ 16  ; snapshot for ps
PS_REC equ 24         ; sys_ps_info record size: state+vbase+esp+name[16]

; returned by get_key
KEY_LEFT  equ 0x80
KEY_RIGHT equ 0x81
KEY_UP    equ 0x82
KEY_DOWN  equ 0x83

; - 800x400x32 graphics (QEMU) -
GFX_W      equ 800
GFX_H      equ 600
GFX_PITCH  equ GFX_W*4                 ; bytes per scanline (32bpp)
GFX_FBSIZE equ GFX_W*GFX_H*4           ; framebuffer byte size
GFX_PAGES  equ (GFX_FBSIZE+4095)/4096  ; 4KB pages to map the LFB
LFB_VIRT   equ 0xC1400000              ; virtual base for the LFB (free PDE 773)
VBE_INDEX  equ 0x01CE                  ; index port
VBE_DATA   equ 0x01CF                  ; data port

; - VGA mode 0x13 (320x200x256, linear framebuffer at 0xA0000) -
M13_FB     equ 0xC00A0000              ; framebuffer (phys 0xA0000, identity-mapped)
M13_W      equ 320
M13_H      equ 200

start:
    jmp short .init_runtime
    nop

    db "KERN"       
    dd (kern_end-start) 

; Multiboot v1 header (a.out kludge) 
align 4
.mb_header:
    dd 0x1BADB002                         ; magic
    dd 0x00010000                         ; flags: a.out kludge
    dd -(0x1BADB002 + 0x00010000)         ; checksum
    dd .mb_header - 0xC0000000            ; header_addr (physical)
    dd start     - 0xC0000000             ; load_addr  = 0x00100000
    dd 0                                  ; load_end_addr (0 = load whole file)
    dd 0                                  ; bss_end_addr  (0 = don't zero bss)
    dd start     - 0xC0000000             ; entry_addr  = 0x00100000

.init_runtime:
    cli
    mov esp,0x00090000       ; temporary physical stack

    call .get_pc
.get_pc:
    pop ebp
    sub ebp,.get_pc          ; ebp = physical_load_addr-virtual_base

    ; GDT
    lea eax,[boot_gdt+ebp]
    mov [boot_gdt_desc.base+ebp],eax      ; patch base to physical (paging is off)
    lea eax,[boot_gdt_desc+ebp]
    lgdt [eax]
    lea eax,[.reload+ebp]                 ; far-return into CS=0x08 at physical .reload
    push dword 0x08
    push eax
    retf
.reload:
    mov ax,0x10     ; DATA_SEG (GDT)
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax

    ; - physical stack -
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

; GRUB's selectors differ. Flat 4 GB, code=0x08 / data=0x10 
align 8
boot_gdt:
    dq 0x0000000000000000        ; null
    dq 0x00CF9A000000FFFF        ; 0x08 code, ring 0, flat 4 GB
    dq 0x00CF92000000FFFF        ; 0x10 data, ring 0, flat 4 GB
boot_gdt_end:
boot_gdt_desc:
    dw boot_gdt_end-boot_gdt-1
.base:
    dd boot_gdt              

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
    call set_freq       
    call dydx_init
    call hwclock

    mov edi,cwd_buf
    mov ecx,128
    xor eax,eax
    rep stosb
    mov byte [cwd_buf],'/'

    call load_fs_persist
    call cls
    call login        
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax

    call init_tasks

    mov [task0_esp],esp 
    mov dword [current_task],0 
    sti   

.task0_entry:
    cmp byte [banner_mode],0  
    jne .skip
    cmp byte [gfx_mode],0      ; no banner while in graphics mode
    jne .skip
    cmp byte [mode13],0        ; also in mode 13h
    jne .skip
    cmp byte [tick_flag],0
    je .skip
    mov byte [tick_flag],0
    call sys_banner
    call sys_tick
    call sys_hertz
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
    cmp al,KEY_LEFT
    je .left
    cmp al,KEY_RIGHT
    je .right
    cmp al,KEY_UP
    je .up
    cmp al,KEY_DOWN
    je .down
    cmp al,32               
    jb .done
    call line_insert          ; insert char at the cursor
    jmp .done
.enter:
    mov ebx,[cmd_len]
    mov byte [cmd_buf+ebx],0
    call newline
    call save_history
    call parse_args
    call check_bg
    call dispatch_command
    mov dword [cmd_len],0     ; reset buffer
    mov dword [cmd_pos],0
    mov byte [cmd_buf],0
    call prompt
    jmp .task0_entry
.tab:
    call tab_complete
    jmp .done
.del:
    call line_backspace
    jmp .done
.left:
    cmp dword [cmd_pos],0
    jbe .done
    dec dword [cmd_pos]
    call line_setcursor
    jmp .done
.right:
    mov eax,[cmd_pos]
    cmp eax,[cmd_len]
    jae .done
    inc dword [cmd_pos]
    call line_setcursor
    jmp .done
.up:
    call hist_back
    jmp .done
.down:
    call hist_frwd
    jmp .done
.done:
    ; (polling) hlt might cause input lag or freeze.
    jmp .task0_entry

task1_entry:
    mov byte [tick_flag], 0
    inc dword [tick_div]
    test dword [tick_div], 1
    jnz .sleep
    mov byte [tick_flag], 1
.sleep:
    hlt      ; run when timer fires                 
    jmp task1_entry     

task2_entry:  
    test dword [on_off],1
    jnz .paused
    call dydx_step
.paused:
    hlt      ; run when state changes
    jmp task2_entry

sys_hertz:
    push eax
    push edi
    mov eax,[hz]
    mov edi,0xC00B8000+(1*80+59)*2  
    call int2str
    mov byte [edi],' '
    add edi,2
    mov byte [edi],'H'
    mov byte [edi+1],0x0C
    add edi,2
    mov byte [edi],'z'
    mov byte [edi+1],0x0C
    add edi,2
    mov byte [edi],' '
    mov byte [edi+1],0x00
    add edi,2
    mov byte [edi],' '
    mov byte [edi+1],0x00
    add edi,2

    pop edi
    pop eax
    ret
    
hwclock:
wait_cmos:
    mov al,0x0A
    out 0x70,al
    in  al,0x71
    test al,0x80
    jnz wait_cmos

    ; Years since 1970 * 365
    mov eax,[year]
    sub eax,1970
    mov ebx,365
    mul ebx
    mov esi,eax

    ; Leap years since 1970
    mov eax,[year]
    sub eax,1969
    xor edx,edx
    mov ebx,4
    div ebx
    add esi,eax

    mov eax,[year]
    sub eax,1901
    xor edx,edx
    mov ebx,100
    div ebx
    sub esi,eax

    mov eax,[year]
    sub eax,1601
    xor edx,edx
    mov ebx,400
    div ebx
    add esi,eax

    mov ecx,[month]
    cmp ecx,1
    jle .months_done
    dec ecx
    mov ebx,month_days
    .mloop:
    movzx eax,byte [ebx]
    add esi,eax
    inc ebx
    dec ecx
    jnz .mloop

.months_done:
    mov eax,[day]
    dec eax
    add esi,eax
    mov eax,esi
    mov ebx,86400
    mul ebx
    mov esi,eax
    mov eax,[hour]
    mov ebx,3600
    mul ebx
    add esi,eax
    mov eax,[min]
    mov ebx,60
    mul ebx
    add esi,eax
    mov eax,[sec]
    add esi,eax
    mov [boot_epoch], esi
    ret


bcd2bin:               ; not in use (I'll keep it anyway)
    push ebx
    mov bl,al
    shr al,4
    mov bh,10
    mul bh             ; AL=tens*10
    and bl,0x0F        ; BL=ones
    add al,bl          ; AL=binary result
    pop ebx
    ret

int2str:
    push ebx
    push ecx
    push edx
    push esi
    mov ebx,10
    xor ecx,ecx
    test eax,eax
    jnz .convert
    mov byte [edi],'0'
    add edi,2
    jmp .done
.convert:
.repeat:
    xor edx,edx
    div ebx             ; EAX=quotient EDX=remainder
    add dl,'0'
    push edx
    inc ecx
    test eax,eax
    jnz .repeat
.print:
    pop edx
    mov [edi],dl
    add edi,1
    mov byte [edi],0x0C
    add edi,1
    loop .print
.done:
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

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

;--- "cmd &" or "cmd&" ---
; clears the '&' from argv and sets bg_flag.
check_bg:
    mov dword [bg_flag],0
    mov ecx,[argc]
    test ecx,ecx
    jz .ret
    mov eax,ecx
    dec eax
    mov esi,[argv+eax*4]       ; last argument
    cmp byte [esi],'&'
    jne .check_suffix
    cmp byte [esi+1],0
    jne .check_suffix
    mov dword [bg_flag],1
    dec dword [argc]           ; drop the &
    ret
.check_suffix:
    ; cmd&
    mov edi,esi
.find_end:
    mov al,[edi]
    test al,al
    jz .test_amp
    inc edi
    jmp .find_end
.test_amp:
    cmp edi,esi                
    je .ret
    cmp byte [edi-1],'&'
    jne .ret
    mov byte [edi-1],0          ; strip the '&'
    mov dword [bg_flag],1
.ret:
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
    mov [cmd_pos],ecx     ; cursor at end of recalled line
    call redraw_line
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
    mov [cmd_pos],ecx
    call redraw_line
    popad
    ret
.empty_line:
    mov dword [cmd_len],0
    mov dword [cmd_pos],0
    call redraw_line
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


; -------------------------------------
; lookup table dispatcher
; -------------------------------------
; every command is a /bin executable.
dispatch_command:
    call exec_bin
    ret

; ---------------------------------------------------------------
;   Reads "exec.cmd" 
;   each line as a shell command, in order.
;   Invoked by the `run` command (syscall 50)/any int 0x80 caller.
; ---------------------------------------------------------------
sys_run:
    pushad
    mov esi,run_file             ; "exec.cmd"
    mov edi,resolve_buf
    call fs_resolve              ; resolve_buf = absolute path
    mov esi,resolve_buf
    call fs_lookup               ; eax = entry ptr or 0
    test eax,eax
    jz .nofile
    cmp dword [eax+FS_NAME_LEN],1    ; must be a regular file (type 1)
    jne .nofile
    mov esi,[eax+FS_NAME_LEN+4]      ; file data ptr
    mov ecx,[eax+FS_NAME_LEN+8]      ; file size
    mov [run_ptr],esi
    mov [run_rem],ecx
.next_line:
    mov ecx,[run_rem]
    test ecx,ecx
    jz .done
    mov esi,[run_ptr]
    mov edi,cmd_buf
    xor edx,edx                     ; line length so far
.chars:
    test ecx,ecx
    jz .eol
    mov al,[esi]
    inc esi
    dec ecx
    cmp al,10                       ; \n ends the line
    je .eol
    cmp al,13                       ; \r ends the line
    je .eol
    cmp edx,63                      ; cap the command line at 63 chars
    jae .chars
    mov [edi],al
    inc edi
    inc edx
    jmp .chars
.eol:
    mov byte [edi],0                ; null-terminate cmd_buf
    mov [run_ptr],esi               ; save cursor across the dispatch
    mov [run_rem],ecx
    test edx,edx
    jz .next_line                   ; skip blank lines
    call parse_args
    call check_bg
    call dispatch_command           ; runs it; "command not found" if unknown
    jmp .next_line
.nofile:
    mov esi,run_nofile_msg
    call print_cr
.done:
    popad
    xor eax,eax
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

; ---------------------------------------------------------------
; cmd_buf holds the line, cmd_len its length,
; cmd_pos the cursor index. 
; The line is drawn starting at prompt_limit
; ---------------------------------------------------------------

; repaint the whole command line and place the hardware
; cursor at prompt_limit+cmd_pos. 
redraw_line:
    pushad
    mov ebx,[prompt_limit]
    mov edi,0xC00B8000
    lea edi,[edi+ebx*2]
    mov ecx,64
    mov ax,0x0720              ; blank?
.clear:
    mov [edi],ax
    add edi,2
    loop .clear
    mov ebx,[prompt_limit]
    mov edi,0xC00B8000
    lea edi,[edi+ebx*2]
    mov esi,cmd_buf
    mov ecx,[cmd_len]
    mov ah,0x02               ; same green attribute as putchar
.paint:
    test ecx,ecx
    jz .place
    mov al,[esi]
    mov [edi],ax
    add edi,2
    inc esi
    dec ecx
    jmp .paint
.place:
    mov eax,[prompt_limit]
    add eax,[cmd_pos]
    mov [cursor_pos],eax
    call cursor
    popad
    ret

; move only the hardware cursor to match cmd_pos.
; Got no software cursor = Breakouts "undocumented feature"
line_setcursor:
    push eax
    mov eax,[prompt_limit]
    add eax,[cmd_pos]
    mov [cursor_pos],eax
    call cursor
    pop eax
    ret

; insert AL at cmd_pos, shifting the tail right, then redraw.
line_insert:
    push eax
    push ecx
    push edx
    mov ah,al                 ; stash the char (al is reused below)
    mov ecx,[cmd_len]
    cmp ecx,63                ; leave room for the null terminator
    jae .ret
    mov edx,[cmd_pos]
.shift:
    cmp ecx,edx               ; move buf[i-1]->buf[i] for i=cmd_len..cmd_pos+1
    jle .put
    mov al,[cmd_buf+ecx-1]
    mov [cmd_buf+ecx],al
    dec ecx
    jmp .shift
.put:
    mov al,ah
    mov [cmd_buf+edx],al
    inc dword [cmd_len]
    inc dword [cmd_pos]
    call redraw_line
.ret:
    pop edx
    pop ecx
    pop eax
    ret

line_backspace:
    push eax
    push esi
    push edi
    mov esi,[cmd_pos]
    test esi,esi
    jz .ret                   ; nothing to the left of the cursor
    dec dword [cmd_pos]
    mov edi,[cmd_pos]         ; write index = cmd_pos-1
    mov esi,edi
    inc esi                   ; read index = cmd_pos
.shift:
    cmp esi,[cmd_len]
    jae .fin
    mov al,[cmd_buf+esi]
    mov [cmd_buf+edi],al
    inc esi
    inc edi
    jmp .shift
.fin:
    dec dword [cmd_len]
    call redraw_line
.ret:
    pop edi
    pop esi
    pop eax
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
    mov [prompt_limit],ebx   
    ret

cursor: 
    push eax
    push edx
    push ebx               
    mov bx,[cursor_pos]    
    mov dx,0x3D4
    mov al,0x0F
    out dx,al
    
    mov dx,0x3D5
    mov al,bl        
    out dx,al

    mov dx,0x3D4
    mov al,0x0E
    out dx,al
    
    mov dx,0x3D5
    mov al,bh        
    out dx,al

    pop ebx
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
;    "Improved ;-)"  
; The oldest form of 
; kernel communication
;---------------------
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

; ---------------------------------------------------------
login:
    pushad
.attempt:
    mov esi,login_user_msg
    call print
    mov byte [login_echo],1     ; show the username
    mov edi,login_ubuf
    call login_readline

    mov esi,login_pass_msg
    call print
    mov byte [login_echo],0     ; mask the passwd with '*'
    mov edi,login_pbuf
    call login_readline

    mov esi,login_ubuf          ; eax=0 when equal
    mov edi,login_root
    call str_eq
    test eax,eax
    jnz .bad
    mov esi,login_pbuf
    mov edi,login_secret
    call str_eq
    test eax,eax
    jnz .bad

    mov esi,login_ok_msg
    call print_cr
    popad
    ret
.bad:
    mov esi,login_bad_msg
    call print_cr
    jmp .attempt

;  Echoes each char,
;  or '*' when [login_echo]=0. 
login_readline:
    push ebx
    push ecx
    xor ecx,ecx
.next:
    call login_getchar           
    test al,al
    jz .next
    cmp al,13
    je .done
    cmp al,8
    je .bs
    cmp al,32
    jb .next                       ; ignore other codes
    cmp ecx,63
    jae .next                      
    mov [edi+ecx],al
    inc ecx
    cmp byte [login_echo],0
    jne .echo
    mov al,'*'
.echo:
    call putchar
    jmp .next
.bs:
    test ecx,ecx
    jz .next
    dec ecx
    call login_del
    jmp .next
.done:
    mov byte [edi+ecx],0
    call newline
    pop ecx
    pop ebx
    ret

login_getchar:
    push ebx
.wait:
    in al,0x64
    test al,1                      ; output buffer full?
    jz .wait
    in al,0x60                     ; scancode
    mov bl,al
    cmp bl,0x2A
    je .son
    cmp bl,0x36
    je .son
    cmp bl,0xAA
    je .soff
    cmp bl,0xB6
    je .soff
    test bl,0x80                   ; key release -> ignore
    jnz .wait
    movzx eax,bl
    cmp byte [login_shift],0
    jne .sh
    mov al,[keymap+eax]
    jmp .ret
.sh:
    mov al,[keymap_shift+eax]
.ret:
    pop ebx
    ret
.son:
    mov byte [login_shift],1
    jmp .wait
.soff:
    mov byte [login_shift],0
    jmp .wait

login_del:
    push eax
    push edi
    mov eax,[cursor_pos]
    test eax,eax
    jz .ret
    dec eax
    mov [cursor_pos],eax
    mov edi,0xC00B8000
    lea edi,[edi+eax*2]
    mov word [edi],0x0720          ; blank cell (green attr)
    call cursor
.ret:
    pop edi
    pop eax
    ret

; --------------------------------------
; Ego boost at boot - look what we built
; --------------------------------------
sys_banner:
    mov esi,sys_msg
    mov edi,0xC00B8000+(0*80+0)
    mov ah,0x03
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
    ;mov eax,0x07200720    ; green+space
    mov eax,0x0A200A20     
    rep stosd
    xor eax,eax
    mov eax,80*3
    mov [cursor_pos],eax
    call cursor
    ret

scroll:
    push esi
    push edi
    push ecx
    push eax
    mov esi,0xC00B8000+(80*4)*2
    mov edi,0xC00B8000+(80*3)*2
    mov ecx,80*23
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

    ; --- arrow keys: return values the shell loop handles them ---
    cmp al,0x4B            ; left
    jne .ck_right
    mov al,KEY_LEFT
    ret
.ck_right:
    cmp al,0x4D            ; right
    jne .ck_up
    mov al,KEY_RIGHT
    ret
.ck_up:
    cmp al,0x48            ; up
    jne .ck_down
    mov al,KEY_UP
    ret
.ck_down:
    cmp al,0x50            ; down
    jne .translate
    mov al,KEY_DOWN
    ret
.translate:
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
    call cls
    call login     
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    ret
    ;cli
    ;mov dx,0xF4
    ;mov al,0
    ;out dx,al
    ;hlt

; --- print 8 hex digits at top-right ----
; Counting heartbeats of the system clock
sys_tick:
    pushad
    mov eax,[tick_count]
    mov edi,0xC00B8000+(1*80+71)*2  
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
    mov dh,0x0C
    mov dh,0x07
    mov [edi],dx
    shl eax,4
    add edi,2
    dec ecx
    jnz .next
    popad
    ret

init_tasks:
    mov eax,task1_stack_top
    mov edx,task1_entry
    call seed_frame
    mov [task1_esp],eax

    mov eax,task2_stack_top
    mov edx,task2_entry
    call seed_frame
    mov [task2_esp],eax
    ret

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


;-----------------------------------------------------
; Persistence layout: FS region starts at LBA 256.
; Each spare slot occupies SECTORS_PER_SLOT sectors.
;   sector 0 — 68-byte fs_entry record, zero-padded
;   sectors 1..4 — FS_CAPACITY (2048) bytes of content
;-----------------------------------------------------
FS_BASE_LBA       equ 512
SECTORS_PER_SLOT  equ 5

; PIO read of ecx sectors from LBA eax into [edi]
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

; PIO write of ecx sectors to LBA eax from [esi]
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

;------------------------------------------------
;   in: 
;      eax = fs_entries entry ptr
;   writes the slot to disk if it's a spare slot; 
;------------------------------------------------
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

    ; ata_write 3 sectors at FS_BASE_LBA + spare_idx*3 
    mov eax,ebp
    imul eax,SECTORS_PER_SLOT
    add eax,FS_BASE_LBA
    mov ecx,SECTORS_PER_SLOT
    mov esi,persist_buf
    call ata_write
.skip:
    popad
    ret

;-------------------------------------------
; called at boot. Reads each spare slot from 
; disk and restores its entry + content.
;-------------------------------------------
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

; -------------------------------------
;   complete the word under the cursor. 
;   First /bin later words against the
;   files/dirs in the current directory.
tab_complete:
    pushad
    mov ecx,[cmd_len]
    test ecx,ecx
    jz .out

    xor edx,edx                   ; word_start
    xor ebx,ebx
.ws:
    cmp ebx,ecx
    jae .ws_done
    cmp byte [cmd_buf+ebx],' '
    jne .ws_n
    lea edx,[ebx+1]
.ws_n:
    inc ebx
    jmp .ws
.ws_done:
    lea eax,[cmd_buf+edx]
    mov [tab_pfx],eax    ; prefix pointer
    mov eax,ecx
    sub eax,edx
    mov [tab_pfxlen],eax ; prefix length
    xor eax,eax
    test edx,edx
    setne al             ; mode 1 = argument (word_start > 0), 0 = command
    mov [tab_mode],al

    ; pass 1: count matches 
    mov byte [tab_printing],0
    call tab_scan
    mov edx,[tab_match_count]
    test edx,edx
    jz .out
    cmp edx,1
    je .do_complete

    ; --- multiple matches
    call newline
    mov byte [tab_printing],1
    call tab_scan
    call newline
    call prompt
    call redraw_line
    jmp .out

.do_complete:
    ; append the part of the match
    mov esi,[tab_single_ptr]
    add esi,[tab_pfxlen]
.cp:
    mov al,[esi]
    test al,al
    jz .cp_sp
    mov ebx,[cmd_len]
    cmp ebx,62
    jae .fin
    mov [cmd_buf+ebx],al
    inc dword [cmd_len]
    inc esi
    jmp .cp
.cp_sp:
    mov ebx,[cmd_len]            
    cmp ebx,63
    jae .fin
    mov byte [cmd_buf+ebx],' '
    inc dword [cmd_len]
.fin:
    mov eax,[cmd_len]
    mov [cmd_pos],eax
    call redraw_line
.out:
    popad
    ret

tab_scan:
    mov dword [tab_match_count],0
    mov dword [tab_single_ptr],0
    cmp byte [tab_mode],0
    jne .args

    ; /bin executables 
    mov esi,fs_entries
    mov ecx,FS_COUNT
.fe:
    test ecx,ecx
    jz .ret
    push esi
    push ecx
    call tab_basename
    test eax,eax
    jz .fe_next
    cmp dword [esi+FS_NAME_LEN],2
    jne .fe_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .fe_next
    call tab_hit
.fe_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .fe
.ret:
    ret

    ; argument mode
.args:
    mov esi,fs_entries
    mov ecx,FS_COUNT
.ae:
    test ecx,ecx
    jz .ret2
    push esi
    push ecx
    mov edi,cwd_buf
    call basename_if_child    
    test eax,eax
    jz .ae_next
    mov esi,eax
    call tab_prefix_match
    test eax,eax
    jz .ae_next
    call tab_hit
.ae_next:
    pop ecx
    pop esi
    add esi,FS_REC_SIZE
    dec ecx
    jmp .ae
.ret2:
    ret

; record/print a match. esi=null-terminated name.
tab_hit:
    inc dword [tab_match_count]
    mov [tab_single_ptr],esi
    cmp byte [tab_printing],0
    je .ret
    push esi
    push eax
.pr:
    mov al,[esi]
    test al,al
    jz .sp
    call putchar
    inc esi
    jmp .pr
.sp:
    mov al,' '
    call putchar
    call putchar
    pop eax
    pop esi
.ret:
    ret

tab_prefix_match:
    push esi
    push edi
    push ecx
    mov edi,[tab_pfx]
    mov ecx,[tab_pfxlen]
    test ecx,ecx
    jz .match             ; empty prefix match all
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


; --------------------------------------------
; - Building the interrupt handler phonebook -
; --------------------------------------------
build_idt:
    mov edi, idt_start
    mov ecx, 256
.fill:
    mov eax,isr_default
    mov [edi],ax                ; Offset low (0-15)
    mov word [edi+2],0x08       ; CODE_SEG 
    mov byte [edi+4],0
    mov byte [edi+5],10001110b  ; Present, Ring 0, 32-bit Interrupt Gate
    shr eax, 16
    mov [edi+6],ax              ; Offset high (16-31)
    add edi, 8
    loop .fill

    mov eax, page_fault_isr
    mov ebx, 14                 ; Page Fault vector index
    mov edi, idt_start
    call set_idt_entry
    ret

set_idt_entry:
    push eax
    push edx                 
    mov edx,eax
    mov word [edi+ebx*8+0],dx  ; offset low
    mov word [edi+ebx*8+2],0x08 
    mov byte [edi+ebx*8+4],0
    mov byte [edi+ebx*8+5],10001110b
    shr edx, 16
    mov word [edi+ebx*8+6],dx  ; offset high
    pop edx
    pop eax             
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

; syscall (int 0x80) 
set_syscall:
    mov eax,syscall_isr
    mov edi,idt_start+0x80*8
    mov word [edi],ax
    mov word [edi+2],8
    mov byte [edi+4],0
    mov byte [edi+5],11101110b   ; 32-bit int gate
    shr eax,16
    mov word [edi+6],ax
    ret

; ---------------------------------------------------------
; int 0x80 dispatcher
;   in : eax = syscall #
;        ebx,ecx,edx,esi,edi = args
;   out: eax = return value (-1 on bad #)
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
    call [syscall_table+eax*4]
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

;  syscall handlers
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

sys_get_arg:         ; ebx = index out:  esi=dest 
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

;  esi=path, ebx=buf, ecx=n -> append n bytes to an existing file.
sys_append:
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
    cmp dword [eax+FS_NAME_LEN],1     ; must be a regular file
    jne .err
    mov ebx,[eax+FS_NAME_LEN+8]       ; current size = append offset
    mov ecx,[esp+0]                   ; n
    mov edx,ebx
    add edx,ecx
    cmp edx,FS_CAPACITY               ; size + n must fit
    ja .err
    mov esi,[esp+4]                   ; src buffer
    mov edi,[eax+FS_NAME_LEN+4]       ; data ptr
    add edi,ebx                       ; + size
    cld
    rep movsb
    mov ecx,[esp+0]
    add ecx,ebx                       ; new size = old size+n
    mov [eax+FS_NAME_LEN+8],ecx
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

;   esi=path -> eax=0/-1
;   Marks slot free
;   so it can be reused by a future sys_create.
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
    mov byte  [eax],0               
    mov dword [eax+FS_NAME_LEN],0     
    mov dword [eax+FS_NAME_LEN+8],0  
    call persist_entry             
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
    test ebx,ebx
    je .error
    push ecx
    push edx
    push esi
    push edi

    mov edx,[current_task]
    mov [ebx+0],edx
    mov dword [ebx+4],MAX_PROC

    xor ecx,ecx                 ; slot index
.slot:
    cmp ecx,MAX_PROC
    jae .done
    mov eax,ecx
    imul eax,PS_REC
    lea edi,[ebx+8]
    add edi,eax
    mov edx,[proc_state+ecx*4]
    mov [edi+0],edx
    mov edx,[proc_vbase+ecx*4]
    mov [edi+4],edx
    mov edx,[task_esps+ecx*4]
    mov [edi+8],edx
    mov eax,ecx
    shl eax,4                   ; *PROC_NAME_LEN (16)
    lea esi,[proc_name+eax]
    add edi,12
    push ecx
    mov ecx,PROC_NAME_LEN
.cpname:
    mov al,[esi]
    mov [edi],al
    inc esi
    inc edi
    loop .cpname
    pop ecx
    inc ecx
    jmp .slot
.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    mov eax,MAX_PROC
    ret
.error:
    mov eax,-1
    ret

sys_stack_dump:
    pushad
    mov edi,esp
    add edi,32           ; Skip past of PUSHAD registers!
    mov ecx,8
.loop:
    mov eax, [edi]       
    call print_hex_dword
    call newline
    add edi,4
    loop .loop
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
    call sys_hex_byte
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

sys_plot:
    pushad
    mov eax,[argc]
    cmp eax,3      
    jl .usage
    mov esi,[argv+4]
    call sys_asc2int  
    mov ecx,edx
    cmp eax,80
    jge .usage

    mov esi,[argv+8]
    call sys_asc2int

    mov eax,edx
    mov edx,ecx
    mov ecx,eax

    imul ecx,80         
    add ecx,edx   
    shl ecx,1            
    mov edi,0xC00B8000
    add edi,ecx
    mov byte [edi],0xDB
    inc edi
    mov byte [edi],0x06 
    popad
    ret
.usage:
    mov esi,plot_msg
    call print_cr
    popad
    ret

sys_epoch:
    mov eax,[boot_epoch]   
    call print_dec
    mov eax,3
    int 0x80
    ret

;--------------------------------------
; in: 
;     ebx = target PID to terminate
; out: 
;     eax = 0 on success, -1 on failure
;--------------------------------------
sys_kill:
    push ecx
    push edx
    push esi
    push edi

    mov edx,ebx       ; PID from user space
    cmp edx,MAX_PROC
    jae .failed
    
    cmp dword [proc_state+edx*4],0
    je .failed           ; Process is already dead/empty

    cmp edx, 0
    je .failed

    cmp edx, [current_task]  ; Don't kill the caller itself
    je .failed               

    cli                      ; While rewriting page tables

    mov ebx, [proc_pages+edx*4]  
    test ebx,ebx
    jz .clear_structures        

    mov eax,[proc_vbase+edx*4]  
    push edx
    call free_pages          ; Unmaps and frees physical frames
    pop edx

    mov ecx,alloc_table_count
    mov edi,alloc_table
    mov esi,[proc_vbase+edx*4]  
.find_slot:
    cmp [edi],esi
    je .clear_slot
    add edi,8
    loop .find_slot
    jmp .clear_structures

.clear_slot:
    mov dword [edi],0          
    mov dword [edi+4],0       

.clear_structures:
    mov dword [proc_vbase+edx*4],0
    mov dword [proc_pages+edx*4],0
    mov dword [proc_state+edx*4],0   

    sti                      
    mov eax,0          
    pop edi
    pop esi
    pop edx
    pop ecx
    ret

.failed:
    mov eax,-1
    pop edi
    pop esi
    pop edx
    pop ecx
    ret

sys_detach:
    mov byte [detach_req],1
    xor eax,eax
    ret

sys_banner_flip:
    call cls
    xor dword [banner_mode],1
    ret

; -------------------------------------------
; All work and no play makes Jack a dull boy
; -------------------------------------------
sys_dydx:
    xor dword [on_off],1
    ret

dydx_init:
    pusha
    mov word [on_off],1
    mov byte [xpos],40
    mov byte [ypos],12
    mov byte [dltx],1
    mov byte [dlty],1
    popa
    ret

dydx_step: 
    call dydx_cls 
    call dydx_update
    call dydx_plot
    ret

dydx_plot:
    pusha
    mov al,[xpos]
    mov ah,[ypos]
    call dydx_calc_offset
    mov ah,0x04
    mov al,0x07 
    mov [edi],ax
    popa
    ret


dydx_cls:
    pusha
    mov al,[xpos]
    mov ah,[ypos]
    call dydx_calc_offset
    mov word [edi],0x0720  
    popa
    ret

dydx_update:
    pusha
    mov al,[xpos]
    add al,[dltx]
    cmp al,79
    jg .x_right
    cmp al,0 
    jl .x_left
    jmp .store_x
.x_right:
    mov al,79
    neg byte [dltx]
    jmp .store_x
.x_left:
    mov al,0 
    neg byte [dltx]
.store_x:
    mov [xpos], al
    mov al,[ypos]
    add al,[dlty]
    cmp al,24
    jg .y_bottom
    cmp al,0
    jl .y_top
    jmp .store_y
.y_bottom:
    mov al,24 
    neg byte [dlty]
    jmp .store_y
.y_top:
    mov al,0
    neg byte [dlty]
.store_y:
    mov [ypos],al
    popa
    ret

dydx_calc_offset:
    push ebx
    movzx ebx,ah        
    imul ebx,160        ; y*80*2
    movzx eax,al        
    shl eax,1           ; x*2
    add ebx,eax         
    mov edi,0xC00B8000
    add edi,ebx         ; gotcha
    pop ebx
    ret

on_off: dd 0
xpos    db 40
ypos    db 12
dltx    db 1
dlty    db 1

;---------------------
; tab completions
;---------------------
basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi+1],0
    je .cwd_root      
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

; ------------------------------
; in:  
;    esi edi = stings (0 term.)
; out: 
;    eax = 0 if equal, 1 if not
; ------------------------------
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

; ----- spawn process -----
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
    dec edi                  
    mov esi,[argv]
.cp_cmd:
    lodsb 
    stosb
    test al,al
    jnz .cp_cmd

    ; -- FS lookup --
    mov esi,path_buf
    call fs_lookup
    test eax,eax
    jz .nf
    cmp dword [eax+FS_NAME_LEN],2   ; type must be 2 (exec)
    jne .nf

    ; save data ptr+size
    mov esi,[eax+FS_NAME_LEN+4]     ; esi = binary data ptr
    mov ebx,[eax+FS_NAME_LEN+8]     ; ebx = byte count

    ; compute page count (round up) 
    mov ecx,ebx
    add ecx,4095
    shr ecx,12                      ; ecx = page count

    push esi
    push ebx
    push ecx
    call find_free_virt             ; eax = vbase, CF=1 on fail
    pop ecx
    pop ebx
    pop esi
    jc .oom
     
    mov [exec_vbase],eax
    mov [exec_pages],ecx

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

    ; copy binary into mapped region
    mov edi,[exec_vbase]
    cld
    rep movsb

    ; register in alloc_table
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    call register_allocation
    

    ; spawn a process in a free table slot
    mov ecx,3
.find_proc:
    cmp ecx,MAX_PROC
    jae .too_many
    cmp dword [proc_state+ecx*4],0
    je .got_proc
    inc ecx
    jmp .find_proc
.got_proc:
    ; snapshot so ps can name the process
    push esi
    push edi
    push edx
    mov eax,ecx
    shl eax,4                        ; * PROC_NAME_LEN (16)
    lea edi,[proc_name+eax]
    mov esi,[argv]                   ; argv[0] = command
    mov edx,PROC_NAME_LEN-1
.cp_name:
    mov al,[esi]
    test al,al
    jz .cp_name_done
    mov [edi],al
    inc esi
    inc edi
    dec edx
    jnz .cp_name
.cp_name_done:
    mov byte [edi],0
    pop edx
    pop edi
    pop esi

    mov eax,ecx
    sub eax,2
    shl eax,12
    add eax,proc_stacks              ; eax = stack top
    mov edx,proc_trampoline          ; entry point
    push ecx
    call seed_frame                  ; eax = task ESP
    pop ecx

    mov [task_esps+ecx*4],eax
    mov ebx,[exec_vbase]
    mov [proc_vbase+ecx*4],ebx
    mov ebx,[exec_pages]
    mov [proc_pages+ecx*4],ebx
    mov byte [detach_req],0          ; fresh process is not self-backgrounded
    mov dword [proc_state+ecx*4],1

    ; background = announce the job id and return 
    cmp dword [bg_flag],0
    je .wait_proc
    mov esi,bg_lbl
    call print
    mov eax,ecx
    call print_int_decimal
    mov esi,bg_lbl2
    call print_cr
    ret

    ; foreground =  until the process exits and frees its slot.
    ; A process can also ask to keep running in the background via sys_detach 
.wait_proc:
    sti
    hlt
    cmp dword [proc_state+ecx*4],0
    je .wait_done                    ; process exited
    cmp byte [detach_req],0
    je .wait_proc                    ; still running, foreground -> keep waiting
    mov byte [detach_req],0          ; process backgrounded itself -> release shell
.wait_done:
    ret

.too_many:
    ; undo the mapping/registration
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    call release_region
    mov esi,too_many_msg
    call print_cr
    ret

.oom_mapped:
    ; partial map: free whatever was mapped
    pop ebx
    pop esi
    mov eax,[exec_vbase]
    mov ebx,[exec_pages]
    sub ebx,ecx          ; pages successfully mapped
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

sys_exit:
    jmp proc_exit

;------------------------------------
; Process-management helpers
;------------------------------------
;   in:  
;      eax = stack top 
;      edx = entry point
;   out: 
;      eax = task ESP 
seed_frame:
    sub eax,4
    mov dword [eax],0x202            ; EFLAGS (IF set)
    sub eax,4
    mov dword [eax],0x08             ; CS
    sub eax,4
    mov [eax],edx                    ; EIP
    sub eax,32                       ; 8 dwords for popad
    mov edi,eax
    mov ecx,8
    xor ebx,ebx
.zero:
    mov [edi],ebx
    add edi,4
    loop .zero
    ret

; next active slot, round-robin.
;   in:  
;      ecx = current slot
;   out: 
;      ecx = next active slot (slot 0 is always active)
next_task:
.scan:
    inc ecx
    cmp ecx,MAX_PROC
    jb .nw
    xor ecx,ecx
.nw:
    cmp dword [proc_state+ecx*4],0
    je .scan
    ret

; free a binary's pages and drop its alloc_table entry.
;   in:  
;      eax = virtual base, ebx = page count
release_region:
    push ecx
    push edx
    push edi
    push eax               
    call free_pages
    pop edx                 
    mov edi,alloc_table
    mov ecx,alloc_table_count
.find:
    cmp [edi],edx
    je .clear
    add edi,8
    loop .find
    jmp .done
.clear:
    mov dword [edi],0
    mov dword [edi+4],0
.done:
    pop edi
    pop edx
    pop ecx
    ret

;------------------------------------
; First thing a spawned process runs (via the scheduler's iretd).
; Calls the binary at its virtual base, then tears the process down.
;------------------------------------
proc_trampoline:
    sti                             
    mov eax,[current_task]
    mov eax,[proc_vbase+eax*4]
    call eax                 ; run binary (ret/int 0x80 -> sys_exit)

;-----------------------------------
; Tear down the current process.
; Not in use, (test for kill [PID])
;-----------------------------------
proc_exit:
    cli
    mov ecx,[current_task]           
    mov ebx,[proc_pages+ecx*4]
    test ebx,ebx
    jz .freed                       
    mov eax,[proc_vbase+ecx*4]
    call release_region              
.freed:
    mov dword [proc_vbase+ecx*4],0
    mov dword [proc_pages+ecx*4],0
    mov dword [proc_state+ecx*4],0   

    ; switch to the next ready task (mirrors irq0's tail) 
    call next_task
    mov [current_task],ecx
    mov esp,[task_esps+ecx*4]
    popad
    iretd

;------------------------------------
irq0:
    pushad
    inc dword [tick_count]
    mov eax,[tick_count]
    xor edx,edx
    ;mov ebx,100
    mov ebx,[hz]
    div ebx
    test edx,edx
    jnz .skip_epoch
    inc dword [boot_epoch]
.skip_epoch:
    mov ecx,[current_task]
    mov [task_esps+ecx*4],esp  ; save outgoing task
    call next_task             ; ecx -> next active slot
    mov [current_task],ecx
    mov esp,[task_esps+ecx*4]  ; load incoming task
.done:
    mov al, 0x20
    out 0x20, al
    popad
    iretd

; -----------------------------------------------------------
; 800x400x32 graphics mode (Bochs/QEMU VBE dispi+linear FB)
; -----------------------------------------------------------
pci_find_vga:
    push ebx
    push ecx
    push edx
    xor ebx,ebx                      ; device number 0..31
.scan:
    mov eax,0x80000000               ; enable | bus 0 | dev<<11 | func 0 | offset 0
    mov ecx,ebx
    shl ecx,11
    or eax,ecx
    mov dx,0xCF8
    out dx,eax
    mov dx,0xCFC
    in eax,dx
    cmp eax,0x11111234               ; (device 0x1111 << 16) | vendor 0x1234
    je .found
    inc ebx
    cmp ebx,32
    jb .scan
    xor eax,eax                      ; not found
    jmp .done
.found:
    mov eax,0x80000000               ; same device, offset 0x10 (BAR0)
    mov ecx,ebx
    shl ecx,11
    or eax,ecx
    or eax,0x10
    mov dx,0xCF8
    out dx,eax
    mov dx,0xCFC
    in eax,dx
    and eax,0xFFFFFFF0               ; mask BAR flags -> LFB physical base
.done:
    pop edx
    pop ecx
    pop ebx
    ret

;   Map the LFB into virtual memory at LFB_VIRT (PDE 773).
;   Runs once; sets gfx_mapped on success. Leaves flag 0 if no VGA.
gfx_map_lfb:
    pushad
    call pci_find_vga
    test eax,eax
    jz .done                         ; no framebuffer -> stay in text mode
    mov [lfb_phys],eax

    ; PDE[773] -> gfx_page_table (phys = virt - 0xC0000000)
    mov eax,gfx_page_table
    sub eax,0xC0000000
    or eax,3
    mov [page_directory + 773*4],eax

    ; fill PTEs: gfx_page_table[i] = (lfb_phys + i*4096) | present|rw
    mov edi,gfx_page_table
    mov eax,[lfb_phys]
    or eax,3
    mov ecx,GFX_PAGES
.fill:
    mov [edi],eax
    add edi,4
    add eax,4096
    dec ecx
    jnz .fill

    mov eax,cr3                      ; flush TLB
    mov cr3,eax
    mov byte [gfx_mapped],1
.done:
    popad
    ret

;  800x400x32 linear framebuffer enabled.
vbe_set:
    push eax
    push edx
    mov dx,VBE_INDEX        ; ENABLE = 0 (disable while reconfiguring)
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    xor ax,ax
    out dx,ax
    mov dx,VBE_INDEX        ; XRES
    mov ax,1
    out dx,ax
    mov dx,VBE_DATA
    mov ax,GFX_W
    out dx,ax
    mov dx,VBE_INDEX        ; YRES
    mov ax,2
    out dx,ax
    mov dx,VBE_DATA
    mov ax,GFX_H
    out dx,ax
    mov dx,VBE_INDEX        ; BPP
    mov ax,3
    out dx,ax
    mov dx,VBE_DATA
    mov ax,32
    out dx,ax
    mov dx,VBE_INDEX        ; ENABLE = ENABLED | LFB
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    mov ax,0x41
    out dx,ax
    pop edx
    pop eax
    ret

;  disable VBE; QEMU reverts to the VGA text mode.
vbe_off:
    push eax
    push edx
    mov dx,VBE_INDEX
    mov ax,4
    out dx,ax
    mov dx,VBE_DATA
    xor ax,ax
    out dx,ax
    pop edx
    pop eax
    ret

;   black.
gfx_clear:
    push eax
    push ecx
    push edi
    mov edi,LFB_VIRT
    xor eax,eax
    mov ecx,GFX_W*GFX_H
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

;  eax=x, ecx=y, ebx=color (0x00RRGGBB)
gfx_plot:
    push edx
    push edi
    mov edi,ecx
    imul edi,GFX_PITCH
    mov edx,eax
    shl edx,2
    add edi,edx
    add edi,LFB_VIRT
    mov [edi],ebx
    pop edi
    pop edx
    ret

hires:
    pushad
    ;call mandelbrot    
    call mandel_simd    
    popad
    ret

; ------------------------------------------------------------------
;   Mandelbrot set across the 800x400 LFB using
;   Q16.16 fixed-point. 
; ------------------------------------------------------------------
MAND_W     equ 800               ; render area (an 800x400 (600) bitmap)
MAND_H     equ 400
MAND_ITER  equ 128
MAND_ESC   equ 0x00040000        ; 4.0 in Q16.16 (escape radius^2)
; view is def at runtime 
; [-2.5,1.0] x [-0.875,0.875] 
MAND_STEP0 equ 287         ; scale: 3.5 / 800 (Q16.16), square pixels
MAND_CX0   equ -49040      ; centre x ~= -0.748
MAND_CY0   equ 0           ; centre y = 0

mandelbrot:
    pushad
    ; top-left corner: x0=cx-(W/2)*step, y0=cy-(H/2)*step
    mov eax,[mand_step]
    imul eax,MAND_W/2
    mov ebx,[mand_cx]
    sub ebx,eax
    mov [m_x0],ebx
    mov eax,[mand_step]
    imul eax,MAND_H/2
    mov ebx,[mand_cy]
    sub ebx,eax
    mov [m_y0],ebx
    xor edi,edi               ; py=0
.row:
    mov eax,edi               ; ci=y0+py*step
    imul eax,[mand_step]
    add eax,[m_y0]
    mov [m_ci],eax
    xor esi,esi               ; px=0
.col:
    mov eax,esi               ; cr=x0+px*step
    imul eax,[mand_step]
    add eax,[m_x0]
    mov [m_cr],eax
    mov dword [m_zr],0        ; z=0
    mov dword [m_zi],0
    xor ecx,ecx               ; iteration count
.iter:
    mov eax,[m_zr]            ; zr2=zr*zr
    mov ebx,eax

    ; 32-bit signed fixed-point
    ; eax=(eax*ebx)>>16
    imul ebx
    shrd eax,edx,16

    mov [m_zr2],eax
    mov eax,[m_zi]            ; zi2=zi*zi
    mov ebx,eax

    imul ebx
    shrd eax,edx,16

    mov [m_zi2],eax
    mov eax,[m_zr2]           ; escape if zr2+zi2>4.0
    add eax,[m_zi2]
    cmp eax,MAND_ESC
    jg .escaped
    mov eax,[m_zr]            ; zi=2*zr*zi+ci 
    mov ebx,[m_zi]

    imul ebx
    shrd eax,edx,16
    
    shl eax,1
    add eax,[m_ci]
    mov ebx,eax               ; hold new zi
    mov eax,[m_zr2]           ; zr=zr2-zi2+cr
    sub eax,[m_zi2]
    add eax,[m_cr]
    mov [m_zr],eax
    mov [m_zi],ebx
    inc ecx
    cmp ecx,MAND_ITER
    jl .iter
    xor ebx,ebx               ; bounded->black
    jmp .plot
.escaped:
    mov eax,ecx               ; color=mandel_colors[iter%MAND_NCOL]
    xor edx,edx
    mov ebx,MAND_NCOL
    div ebx
    mov ebx,[mandel_colors+edx*4]
.plot:
    mov eax,esi               ; x=px
    mov ecx,edi               ; y=py
    call gfx_plot
    inc esi
    cmp esi,MAND_W
    jl .col
    inc edi
    cmp edi,MAND_H
    jl .row
    call corners
    popad
    ret

; ---------------------------------------------
; Mandelbrot view controls (irq1).
; ---------------------------------------------
mand_reset:
    mov dword [mand_cx],MAND_CX0
    mov dword [mand_cy],MAND_CY0
    mov dword [mand_step],MAND_STEP0
    ret
mand_zoom_in:          ; 0.5 the scale->2x zoom
    mov eax,[mand_step]
    shr eax,1
    cmp eax,1
    jge .s
    mov eax,1
.s:
    mov [mand_step],eax
    ret
mand_zoom_out:         ; *2 the scale, clamp at 4096
    mov eax,[mand_step]
    shl eax,1
    cmp eax,4096
    jle .s
    mov eax,4096
.s:
    mov [mand_step],eax
    ret
; pan by ~100 pixels 
mand_pan_left:
    mov eax,[mand_step]
    imul eax,100
    sub [mand_cx],eax
    ret
mand_pan_right:
    mov eax,[mand_step]
    imul eax,100
    add [mand_cx],eax
    ret
mand_pan_up:
    mov eax,[mand_step]
    imul eax,100
    sub [mand_cy],eax
    ret
mand_pan_down:
    mov eax,[mand_step]
    imul eax,100
    add [mand_cy],eax
    ret

; ------------------------------------------------------------------
;   Turn on SSE (clear CR0.EM, set CR0.MP, set CR4.OSFXSR|OSXMMEXCPT).
;   Must run at CPL 0. The kernel is otherwise integer-only.
; ------------------------------------------------------------------
sse_enable:
    push eax
    mov eax,cr0
    and eax,0xFFFFFFFB          ; clear EM (bit 2)
    or  eax,0x00000002          ; set MP (bit 1)
    mov cr0,eax
    mov eax,cr4
    or  eax,0x00000600          ; set OSFXSR (9) | OSXMMEXCPT (10)
    mov cr4,eax
    pop eax
    ret

; ------------------------------------------------------------------
;   Mandelbrot on the 800x400x32 LFB using SSE — 4 pixels per
;   iteration loop (packed single-precision floats). 
; ------------------------------------------------------------------
mandel_simd:
    pushad
    call sse_enable
    xor edi,edi                  ; py=0
.row:
    xor esi,esi                  ; px=0 (group of 4)
.col:
    cvtsi2ss xmm2,esi            ; cr=xmin+px*step,then bcast+lane offsets
    mulss xmm2,[sf_step]
    addss xmm2,[sf_xmin]
    shufps xmm2,xmm2,0
    addps xmm2,[sv_lanestep]     ; xmm2=cr for the 4 lanes
    cvtsi2ss xmm3,edi            ; ci=ymin+py*step (same for all 4 lanes)
    mulss xmm3,[sf_step]
    addss xmm3,[sf_ymin]
    shufps xmm3,xmm3,0           ; xmm3=ci
    xorps xmm0,xmm0              ; zr=0
    xorps xmm1,xmm1              ; zi=0
    xorps xmm4,xmm4              ; iter=0
    mov ecx,MAND_ITER
.it:
    movaps xmm5,xmm0             ; zr2=zr*zr
    mulps  xmm5,xmm0
    movaps xmm6,xmm1             ; zi2=zi*zi
    mulps  xmm6,xmm1
    movaps xmm7,xmm5             ; mag=zr2+zi2
    addps  xmm7,xmm6
    cmpleps xmm7,[sv_four]       ; mask=(mag<=4.0) per lane
    movmskps eax,xmm7
    test eax,eax
    jz .esc                      ; all 4 lanes escaped
    andps xmm7,[sv_one]          ; iter++ for lanes still bounded
    addps xmm4,xmm7
    movaps xmm7,xmm0             ; zi=2*zr*zi+ci 
    mulps  xmm7,xmm1
    mulps  xmm7,[sv_two]
    addps  xmm7,xmm3
    subps  xmm5,xmm6             ; zr=zr2-zi2+cr
    addps  xmm5,xmm2
    movaps xmm0,xmm5
    movaps xmm1,xmm7
    dec ecx
    jnz .it
.esc:
    cvttps2dq xmm4,xmm4         ; iteration counts->packed int32
    movaps [simd_iter_buf],xmm4
    xor edx,edx                 ; lane=0..3
.plot:
    push edx
    mov eax,[simd_iter_buf+edx*4]
    cmp eax,MAND_ITER
    jae .black                  ; never escaped -> interior
    xor edx,edx
    mov ecx,MAND_NCOL
    div ecx                     ; edx=iter%MAND_NCOL
    mov ebx,[mandel_colors+edx*4]
    jmp .put
.black:
    xor ebx,ebx
.put:
    pop edx
    mov eax,esi                 ; x=px+lane
    add eax,edx
    mov ecx,edi                 ; y=py
    call gfx_plot            
    inc edx
    cmp edx,4
    jb .plot
    add esi,4                   ; next group of 4 columns
    cmp esi,MAND_W
    jl .col
    inc edi
    cmp edi,MAND_H
    jl .row
    popad
    ret

; ------------------------

gfx_on:
    cmp byte [gfx_mapped],1
    je .ready
    call gfx_map_lfb
    cmp byte [gfx_mapped],1
    jne .fail                       ; mapping failed -> stay in text mode
.ready:
    cmp byte [font_saved],0
    jne .skipfont
    call vga_save_font           
.skipfont:
    call vbe_set
    call gfx_clear
    call mand_reset                
    call hires
    mov byte [gfx_mode],1
.fail:
    ret

;-----------------
corners:
    mov eax,400
    mov ecx,200
    mov ebx,0x00FF88FF
    call gfx_plot

    ; Top-Left (White)
    mov eax,0            ; X = 0
    mov ecx,0            ; Y = 0
    mov ebx,0x00FFFFFF   ; Color = White
    call gfx_plot

    ; Top-Right (Red)
    mov eax,799          ; X = 799 (Max Width)
    mov ecx,0            ; Y = 0
    mov ebx,0x00FF0000   ; Color = Red
    call gfx_plot

    ; Bottom-Left (Green)
    mov eax,0            ; X = 0
    mov ecx,399          ; Y = 399 (Max Height)
    mov ebx,0x0000FF00   ; Color = Green
    call gfx_plot

    ; Bottom-Right (Blue)
    mov eax,799          ; X = 799
    mov ecx,399          ; Y = 399
    mov ebx,0x000000FF   ; Color = Blue
    call gfx_plot
    ret

;  VGA plane 2 (text font) linearly @ 0xA000.
vga_plane2_access:
    push eax
    push edx
    mov dx,0x3C4        ; SEQ reset
    mov al,0x00
    out dx,al
    mov dx,0x3C5
    mov al,0x01
    out dx,al
    mov dx,0x3C4        ; map mask=plane 2
    mov al,0x02
    out dx,al
    mov dx,0x3C5
    mov al,0x04
    out dx,al
    mov dx,0x3C4        ; mem mode=ext, sequential, odd/even off
    mov al,0x04
    out dx,al
    mov dx,0x3C5
    mov al,0x07
    out dx,al
    mov dx,0x3C4        ; end reset
    mov al,0x00
    out dx,al
    mov dx,0x3C5
    mov al,0x03
    out dx,al
    mov dx,0x3CE       ; GC read map select = plane 2
    mov al,0x04
    out dx,al
    mov dx,0x3CF
    mov al,0x02
    out dx,al
    mov dx,0x3CE       ; GC mode = 0
    mov al,0x05
    out dx,al
    mov dx,0x3CF
    mov al,0x00
    out dx,al
    mov dx,0x3CE       ; GC misc = map 0xA0000, planar
    mov al,0x06
    out dx,al
    mov dx,0x3CF
    mov al,0x00
    out dx,al
    pop edx
    pop eax
    ret

; 8x16 font (plane 2) -> font_save.
vga_save_font:
    pushad
    call vga_plane2_access
    mov esi,0xC00A0000
    mov edi,font_save
    mov ecx,8192/4
    rep movsd
    mov byte [font_saved],1
    popad
    ret

vga_restore_font:
    pushad
    cmp byte [font_saved],0
    je .done
    call vga_plane2_access
    mov esi,font_save
    mov edi,0xC00A0000
    mov ecx,8192/4
    rep movsd
.done:
    popad
    ret

;   VGA controller for 80x25 text (mode 0x03).
vga_mode3:
    pushad
    mov dx,0x3C2            ; Misc Output
    mov al,0x67
    out dx,al

    mov esi,vga_seq_m3      ; Sequencer 0..4
    xor ecx,ecx
.seq:
    mov dx,0x3C4
    mov al,cl
    out dx,al
    mov dx,0x3C5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,5
    jb .seq

    mov dx,0x3D4           ; clear protect bit
    mov al,0x11
    out dx,al
    mov dx,0x3D5
    mov al,0x0E
    out dx,al

    mov esi,vga_crtc_m3   
    xor ecx,ecx
.crtc:
    mov dx,0x3D4
    mov al,cl
    out dx,al
    mov dx,0x3D5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x19
    jb .crtc

    mov esi,vga_gc_m3    
    xor ecx,ecx
.gc:
    mov dx,0x3CE
    mov al,cl
    out dx,al
    mov dx,0x3CF
    lodsb
    out dx,al
    inc ecx
    cmp ecx,9
    jb .gc

    mov dx,0x3DA          ; reset attribute flip-flop
    in al,dx
    mov esi,vga_ac_m3     
    xor ecx,ecx
.ac:
    mov dx,0x3C0
    mov al,cl
    out dx,al
    lodsb
    out dx,al             
    inc ecx
    cmp ecx,0x15
    jb .ac
    mov dx,0x3C0
    mov al,0x20           ; re-enable video output, lock palette
    out dx,al
    mov dx,0x3C6          ; PEL mask
    mov al,0xFF
    out dx,al
    popad
    ret
; What a pain in the...
vga_seq_m3:  db 0x03,0x00,0x03,0x00,0x02
vga_crtc_m3: db 0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E
             db 0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF
vga_gc_m3:   db 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF
vga_ac_m3:   db 0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B
             db 0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00

; restore text mode.
gfx_off:
    call vbe_off
    call vga_restore_font   ; repaint the 8x16 text font 
    call vga_mode3          ; bring back the 80x25 text 
    mov byte [gfx_mode],0
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    call newline
    call prompt
    ret

; ESC handler
gfx_toggle:
    cmp byte [gfx_mode],0
    jne .leave
    call gfx_on
    ret
.leave:
    call gfx_off
    ret

;--------------------------------------------------------------
; 320x200x256, linear framebuffer at 0xA0000.
; The `mode13` command enters mode 13h; the shell keeps running 
; `setpixel x y c`, `clearpixel x y` and `clear` 
; work from the (invisible) text prompt.  ;-)
;--------------------------------------------------------------

vga_mode13:
    pushad
    mov dx,0x3C2               ; Misc Output
    mov al,0x63
    out dx,al

    mov esi,vga_seq_m13        ; Sequencer 0..4
    xor ecx,ecx
.seq:
    mov dx,0x3C4
    mov al,cl
    out dx,al
    mov dx,0x3C5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,5
    jb .seq

    mov dx,0x3D4              ; unlock CRTC 0-7
    mov al,0x11
    out dx,al
    mov dx,0x3D5
    mov al,0x0E
    out dx,al

    mov esi,vga_crtc_m13       ; CRTC 0..0x18
    xor ecx,ecx
.crtc:
    mov dx,0x3D4
    mov al,cl
    out dx,al
    mov dx,0x3D5
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x19
    jb .crtc

    mov esi,vga_gc_m13         ; Graphics Controller 0..8
    xor ecx,ecx
.gc:
    mov dx,0x3CE
    mov al,cl
    out dx,al
    mov dx,0x3CF
    lodsb
    out dx,al
    inc ecx
    cmp ecx,9
    jb .gc

    mov dx,0x3DA             ; reset attribute flip-flop
    in al,dx
    mov esi,vga_ac_m13       ; Attribute Controller 0..0x14
    xor ecx,ecx
.ac:
    mov dx,0x3C0
    mov al,cl
    out dx,al
    lodsb
    out dx,al
    inc ecx
    cmp ecx,0x15
    jb .ac
    mov dx,0x3C0
    mov al,0x20              ; unblank
    out dx,al

    call m13_palette
    popad
    ret
; Me made a typo? ;-)
vga_seq_m13:  db 0x03,0x01,0x0F,0x00,0x0E
vga_crtc_m13: db 0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00
              db 0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x40,0x96,0xB9,0xA3
              db 0xFF
vga_gc_m13:   db 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF
vga_ac_m13:   db 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A
              db 0x0B,0x0C,0x0D,0x0E,0x0F,0x41,0x00,0x0F,0x00,0x00

;   256-entry RGB332 palette to the DAC.
;   index bits RRRGGGBB -> 
;   0x00 black, 0xFF white, 0xE0 red, 0x1C green, 0x03 blue.
m13_palette:
    push eax
    push ecx
    push edx
    mov dx,0x3C8           ; DAC write index
    xor al,al
    out dx,al              ; start at colour 0
    mov dx,0x3C9           ; DAC data
    xor ecx,ecx
.loop:
    mov eax,ecx            ; red   = ((i>>5)&7)*9  (0..63)
    shr eax,5
    and eax,7
    imul eax,eax,9
    out dx,al
    mov eax,ecx            ; green = ((i>>2)&7)*9
    shr eax,2
    and eax,7
    imul eax,eax,9
    out dx,al
    mov eax,ecx            ; blue  = (i&3)*21
    and eax,3
    imul eax,eax,21
    out dx,al
    inc ecx
    cmp ecx,256
    jb .loop
    pop edx
    pop ecx
    pop eax
    ret

; fill the framebuffer with color 0
m13_clear:
    push eax
    push ecx
    push edi
    mov edi,M13_FB
    xor eax,eax
    mov ecx,M13_W*M13_H/4
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

;   set one pixel.  eax = x (0..319), ecx = y (0..199), bl = color.
m13_plot:
    push edi
    imul edi,ecx,M13_W       ; y*320
    add edi,eax              ; + x
    add edi,M13_FB
    mov [edi],bl
    pop edi
    ret

;   filled red disc centred on the screen 
;   Scans the bounding box and plots where dx*dx+dy*dy <= r*r.
M13_CX equ M13_W/2
M13_CY equ M13_H/2
M13_R  equ 30
m13_test_circle:
    pushad
    mov esi,-M13_R              ; dy
.yloop:
    mov edi,-M13_R              ; dx
.xloop:
    mov eax,edi
    imul eax,edi                ; dx*dx
    mov ebx,esi
    imul ebx,esi                ; dy*dy
    add eax,ebx
    cmp eax,M13_R*M13_R         ; inside the disc?
    jg .next
    mov eax,M13_CX
    add eax,edi                 ; x=cx+dx
    mov ecx,M13_CY
    add ecx,esi                 ; y=cy+dy
    mov bl,0xE0                 ; RGB332 red
    call m13_plot
.next:
    inc edi
    cmp edi,M13_R
    jle .xloop
    inc esi
    cmp esi,M13_R
    jle .yloop
    popad
    ret

;   Save current 256-entry DAC palette
;   (R,G,B x 256 = 768 bytes)
vga_save_dac:
    pushad
    mov dx,0x3C7            ; DAC read index
    xor al,al
    out dx,al               ; start reading at color 0
    mov dx,0x3C9            ; DAC data
    mov edi,dac_save
    mov ecx,768
.loop:
    in al,dx
    mov [edi],al
    inc edi
    dec ecx
    jnz .loop
    popad
    ret

; write back into the DAC palette.
vga_restore_dac:
    pushad
    mov dx,0x3C8           ; DAC write index
    xor al,al
    out dx,al              ; start writing at color 0
    mov dx,0x3C9           ; DAC data
    mov esi,dac_save
    mov ecx,768
.loop:
    mov al,[esi]
    out dx,al
    inc esi
    dec ecx
    jnz .loop
    popad
    call gfx_off  ;Well... it worked...
    ret

;   enter mode 13h from text mode with a black screen. 
mode13_on:
    cmp byte [gfx_mode],0    ; only from text mode, not from 800x400
    jne .ret
    cmp byte [mode13],0      ; already in mode 13h?
    jne .ret
    cmp byte [font_saved],0
    jne .skipfont
    call vga_save_font       
.skipfont:
    call vga_save_dac         
    call vga_mode13
    call m13_clear
    call m13_test_circle   
    mov byte [mode13],1
.ret:
    ret

;  restore the text console & its palette.
mode13_off:
    call vga_restore_font
    call vga_mode3
    call vga_restore_dac     
    mov byte [mode13],0
    call cls
    call sys_banner
    mov eax,80*2
    mov [cursor_pos],eax
    call newline
    call prompt
    ret

irq1:
    pushad
    in al,0x60
    mov bl,al
    cmp bl,0x01              ; ESC -> 800x400 graphics toggle
    je .esc
    cmp bl,41   ;0x3F        ; § -> mode 13h toggle
    je .pnd
    cmp bl,0x2A
    je .shift_on
    cmp bl,0x36
    je .shift_on
    cmp bl,0xAA
    je .shift_off
    cmp bl,0xB6
    je .shift_off
    test bl,0x80
    jnz .done
    cmp byte [gfx_mode],0   ; 800x400 graphics: keys Mandelbrot view
    jne .gfxkeys
    mov eax,[kbd_head]
    mov [kbd_buf+eax],bl
    inc eax
    and eax,255
    mov [kbd_head],eax
    jmp .done

; Mandelbrot zoom/pan 
.gfxkeys:
    cmp bl,0x4B             ; Left
    je .g_left
    cmp bl,0x4D             ; Right
    je .g_right
    cmp bl,0x48             ; Up
    je .g_up
    cmp bl,0x50             ; Down
    je .g_down
    cmp bl,12 ;0x4E         ; keypad +
    je .g_zin
    cmp bl,53 ;0x4A         ; keypad -
    je .g_zout
    cmp bl,0x13             ; R -> reset view
    je .g_reset
    jmp .done               ; any other key: ignore
.g_left:
    call mand_pan_left
    jmp .g_render
.g_right:
    call mand_pan_right
    jmp .g_render
.g_up:
    call mand_pan_up
    jmp .g_render
.g_down:
    call mand_pan_down
    jmp .g_render
.g_zin:
    call mand_zoom_in
    jmp .g_render
.g_zout:
    call mand_zoom_out
    jmp .g_render
.g_reset:
    call mand_reset
.g_render:
    call mandelbrot
    jmp .done
.esc:
    cmp byte [mode13],0     ; ESC does nothing while in mode 13h
    jne .done
    call gfx_toggle
    jmp .done
.pnd:
    cmp byte [mode13],0     
    je .done
    call mode13_off
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
    pusha 

    in  al,0x21       
    mov ah,al       
    in  al,0xA1      
    push ax           

    mov al,0x11      
    out 0x20,al      
    out 0xA0,al      

    mov al,0x20      
    out 0x21,al
    mov al,0x28      
    out 0xA1,al

    mov al,0x04       
    out 0x21,al
    mov al,0x02      
    out 0xA1,al        

    mov al,0x01         ; 8086/88 (Intel) Mode
    out 0x21,al
    out 0xA1,al

    pop ax              ; AL = Slave mask, AH = Master mask
    out 0xA1,al         ; Restore Slave mask
    mov al,ah           ; Put Master mask back into AL
    out 0x21,al         ; Restore Master mask
    popa
    ret

;   set the PIT timer frequency from the command line.
;   Reads argv[1] (decimal Hz); clamps to 10..2000, default 100
;   Returns eax = Hz.
sys_setfreq:
    push ecx
    push edx
    push esi

    mov edx,100
    mov eax,[argc]
    cmp eax,2
    jl .set_pit

    mov esi,[argv+4]
    call sys_asc2int 

    cmp edx,10
    jl .use_default
    cmp edx,2000
    jg .use_default
    jmp .set_pit
.use_default:
    mov edx,100
.set_pit:
    mov [hz],edx
    mov ecx,edx
    mov eax,1193182
    xor edx,edx
    div ecx                      ; eax  divisor=1193182/hz
    mov ecx,eax
    mov al,0x36
    out 0x43,al
    mov al,cl                    ; divisor low byte
    out 0x40,al
    mov al,ch                    ; divisor high byte
    out 0x40,al
    mov eax,[hz]                 ; return the frequency set
    pop esi
    pop edx
    pop ecx
    ret

;   VGA mode 13h (mode13 command). 
;   F5 or § returns to text mode?
sys_mode13:
    call mode13_on
    xor eax,eax
    ret

;   plot one pixel in mode 13h.
;   ebx = x (0..319), ecx = y (0..199), edx = color (RGB332 index).
sys_setpixel:
    cmp byte [mode13],0
    je .done
    cmp ebx,M13_W
    jae .done
    cmp ecx,M13_H
    jae .done
    mov eax,ebx                  ; m13_plot x in eax
    mov bl,dl                    ; colour in bl 
    call m13_plot                ; eax=x, ecx=y, bl=colour
.done:
    xor eax,eax
    ret

;   clear one pixel in mode 13h.
sys_clearpixel:
    cmp byte [mode13],0
    je .done
    cmp ebx,M13_W
    jae .done
    cmp ecx,M13_H
    jae .done
    mov eax,ebx                  ; x in eax
    xor bl,bl                    ; color 0
    call m13_plot
.done:
    xor eax,eax
    ret

; cls the mode-13h framebuffer to black.
sys_gclear:
    cmp byte [mode13],0
    je .done
    call m13_clear
.done:
    xor eax,eax
    ret

set_freq:
    mov al,0x36
    out 0x43,al
    mov ax,11931        ; al=0x9B (LSB),ah=0x2E (MSB)
    out 0x40,al         
    mov al,ah         
    out 0x40,al       
    ret

sys_msg   db "----------------------------------------"
          db "----------------------------------------"
          db " *** x86 Operating System (EFBEADDE) ***"
          db "       Core timer:                      "
          db "----------------------------------------"
          db "----------------------------------------",0
deadbeef  db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF

eax_lbl db "EAX: ",0
ebx_lbl db "EBX: ",0
ecx_lbl db "ECX: ",0
edx_lbl db "EDX: ",0
esi_lbl db "ESI: ",0
edi_lbl db "EDI: ",0
ebp_lbl db "EBP: ",0
esp_lbl db "ESP: ",0

section .data

tick_flag    db 0
tick_count   dd 0
tick_div     dd 0
cursor_pos   dd 0
prompt_limit dd 0
cmd_pos      dd 0      
gfx_mode     db 0      ; 0 = text mode, 1 = 800x400 graphics
mode13       db 0      ; 1 = VGA mode 0x13 (320x200x256) active
gfx_mapped   db 0     
banner_mode  db 0     
lfb_phys     dd 0      

mandel_colors:
    dd 0x00000764
    dd 0x00001E96
    dd 0x000048C8
    dd 0x000082D8
    dd 0x0000C0E0
    dd 0x0020E0B0
    dd 0x0060F070
    dd 0x00A8F840
    dd 0x00E0F020
    dd 0x00F8C000
    dd 0x00F88800
    dd 0x00F04800
    dd 0x00D01030
    dd 0x00A00880
    dd 0x006018C0
    dd 0x004008A0
mandel_colors_end:
MAND_NCOL equ (mandel_colors_end - mandel_colors) / 4

; --- Mandelbrot view (Q16.16)
mand_cx   dd -49040
mand_cy   dd 0
mand_step dd 287

font_saved   db 0  

; mandel_simd (SSE) constants: packed single floats
align 16
sv_lanestep dd 0.0, 0.004375, 0.00875, 0.013125  ; step * [0,1,2,3]
sv_four     dd 4.0, 4.0, 4.0, 4.0                ; escape radius^2
sv_two      dd 2.0, 2.0, 2.0, 2.0
sv_one      dd 1.0, 1.0, 1.0, 1.0
sf_xmin     dd -2.5
sf_ymin     dd -0.875
sf_step     dd 0.004375      ; 3.5/800 = 1.75/400 (square)
    

login_user_msg db "Login: ",0
login_pass_msg db "Password: ",0
login_ok_msg   db "Access granted",13,0
login_bad_msg  db "Login incorrect",13,0
login_root     db "aug6",0       ;NEVER AGAIN!
login_secret   db "1945",0
login_shift    db 0     
login_echo     db 0              ;1=echo, 0=mask *

;--- buffers ---
kbd_head     dd 0
kbd_tail     dd 0

out_mem         db "OUT OF MEMORY - KEEP DREAMING",13,0
too_many_msg    db 13,"too many processes",13,0
bg_lbl          db "[bg pid ",0
bg_lbl2         db "]",13,0
alloc_mem       db "heap pointer  : 0x",0
no_arg          db 13,'usage: alloc <bytes>',13,0
poke_msg        db 13,"usage: poke <hex addr> <hex value>",13,0
peek_msg        db 13,"usage: peek <hex addr>",13,0
plot_msg        db 13,"usage: plot x y (0-79 x 0-24)" ,13,0   
free_usage_msg  db 13,"usage: free <hex addr>",13,0
free_ok_msg     db "memory released",13,0
bad_free_msg    db "invalid allocation",13,0
pf_msg          db 13,"PAGE FAULT",13,0
pf_addr         db "ADDRESS: 0x",0
command_nf_msg   db 13,"command not found",13,0
run_file         db "exec.cmd",0
run_nofile_msg   db "exec.cmd not found",13,0

hz                     dd 100,0
kernel_phys_start_var: dd 0
kernel_phys_end_var:   dd 0

boot_epoch             dd 0   ; system baseline clock
month_days:            db 31,28,31,30,31,30,31,31,30,31,30,31
sec     dd 0
min     dd 0
hour    dd 0
day     dd 0
month   dd 0
year    dd 0
dec_buf times 11 db 0

bin_prefix   db "/bin/",0
exec_vbase   dd 0
exec_pages   dd 0

argc         dd 0
argv times 16 dd 0
bg_flag      dd 0    ; 1 = exec_bin in background (trailing '&')
detach_req   db 0    ; 1 = running process to self-background (sys_detach)

cmd_len      dd 0

section .data
current_task:  dd 0
task_esps:                           ; saved kernel ESP per process
    task0_esp: dd 0
    task1_esp: dd 0
    task2_esp: dd 0
    dd 0, 0, 0, 0, 0                 ; slots 3..MAX_PROC-1
proc_state:                          ; 0 = free, 1 = active (schedulable)
    dd 1, 1, 1, 0, 0, 0, 0, 0
proc_vbase:                          ; binary virtual base, for cleanup (0 = none)
    dd 0, 0, 0, 0, 0, 0, 0, 0
proc_pages:                          ; binary page count, for cleanup (0 = none)
    dd 0, 0, 0, 0, 0, 0, 0, 0
proc_name:                           ; for ps
    times MAX_PROC*PROC_NAME_LEN db 0

hist_count  dd 0
hist_index  dd 0

;---- INTERRUPT DESC TABLE ----
idt_descriptor:
            dw idt_end-idt_start-1
            dd idt_start

;---- SYSCALL TABLE  (index = eax at int 0x80) ----
syscall_table:
    dd sys_exit          ; 0 : eax = return code
    dd sys_print         ; 1 : esi = string ptr
    dd sys_print_cr      ; 2 : esi = string ptr (CR aware)
    dd sys_newline       ; 3
    dd sys_cls           ; 4
    dd sys_print_hex     ; 5 : ebx = value out=dword
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
    dd sys_write         ; 18: esi = path,ebx=buf,ecx=n
    dd sys_unlink        ; 19: esi = path 
    dd sys_mkdir         ; 20: esi = path  
    dd sys_rmdir         ; 21: esi = path 
    dd sys_ps_info       ; 22: ebx = dst ptr 
    dd sys_stack_dump    ; 23: print top of stack
    dd sys_alloc         ; 24: in = <bytes> -> page ptr 
    dd sys_dealloc       ; 25: in = <page ptr> 
    dd sys_peek          ; 26: in = <address> 
    dd sys_poke          ; 27: in = <address> <value> 
    dd sys_hex2int       ; 28: in = esi out = eax
    dd sys_banner        ; 29: print banner
    dd sys_dydx          ; 30: Test
    dd sys_out_hex       ; 31: in = eax out=print hex word 
    dd sys_mem_dump      ; 32: in = esi -> address. out: print 64 bytes 
    dd sys_hex_byte      ; 33: print hex byte
    dd sys_asc2int       ; 34: in = esi->string out=edx
    dd sys_hertz         ; 35: print current hertz
    dd sys_tick          ; 36: print heartbeats
    dd sys_plot          ; 37: set block at ecx edx 
    dd sys_epoch         ; 38: out: prints unixtime 
    dd sys_putchar       ; 39: ebx = char
    dd sys_kill          ; 40: ebx = PID to terminate
    dd sys_reg_dump      ; 41: print regs
    dd sys_heap_info     ; 42: ebx = dst -> heap alloc table snapshot
    dd sys_detach        ; 43: background the calling process (return to prompt)
    dd sys_setfreq       ; 44: set PIT frequency from argv[1] (Hz)
    dd sys_banner_flip   ; 45: Test
    dd sys_mode13        ; 46: enter VGA mode 13h
    dd sys_setpixel      ; 47: ebx=x ecx=y edx=colour -> plot pixel
    dd sys_clearpixel    ; 48: ebx=x ecx=y -> clear pixel
    dd sys_gclear        ; 49: clear the mode-13h framebuffer
    dd sys_run           ; 50: run commands from exec.cmd one by one
    dd sys_append        ; 51: esi=path,ebx=buf,ecx=n -> append to a file
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
    db ',','.',';',0,'*',0,' '
    times 0x56-($-keymap) db 0     
    db ':'
    times 256-($-keymap) db 0


; -----------------------------------------
; shift keymap
; -----------------------------------------

keymap_shift:
    db 0,27,'!','@','#','$','%','&','/','('
    db ')','=','+','|',8,9
    db 'Q','W','E','R','T','Y','U','I'
    db 'O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K'
    db 'L','<','>','~',0,'*'
    db 'Z','X','C','V','B','N','M'
    db '<','>',':',0,'*',0,' '
    times 0x56-($-keymap_shift) db 0    
    db ':'
    times 256-($-keymap_shift) db 0

;---- in-kernel virtual filesystem ----
%include "fs.inc"

kern_end:

section .bss
;----------------------------
alignb 16

tab_match_count  resd 1
tab_single_ptr   resd 1
tab_pfx          resd 1   
tab_pfxlen       resd 1   
tab_mode         resb 1   
tab_printing     resb 1    

alignb 4
kbd_buf          resb 256
login_ubuf       resb 64
login_pbuf       resb 64
cmd_buf          resb 64
run_ptr          resd 1        ; exec.cmd read cursor
run_rem          resd 1        ; exec.cmd bytes remaining
hist_buf         resb 32*64
dir_buf          resb 512

;---- VFS state ----
cwd_buf          resb 128
resolve_buf      resb 128
path_buf         resb 128
tmp_dst          resd 1
tmp_left         resd 1
persist_buf      resb 2560    ; 5 sectors: metadata+2048 Bytes

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

alignb 16
proc_stacks:        ; stacks for spawned processes (slots 3..MAX_PROC-1)
    resb (MAX_PROC-3)*4096
proc_stacks_top:


;---- STACK ----
alignb 16
stack_bottom:
    resb 16384      ; 16 KB stack  (256 bytes in C64)
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

alignb 4096
gfx_page_table:            ; maps the 800x400 LFB at LFB_VIRT (PDE 773)
    resd 1024

alignb 4
m_zr  resd 1               ; Mandelbrot Q16.16 working values
m_zi  resd 1
m_cr  resd 1
m_ci  resd 1
m_zr2 resd 1
m_zi2 resd 1
m_x0  resd 1               ; view tlc (derived from centre/scale)
m_y0  resd 1


alignb 16   
simd_iter_buf resd 4       ; 4 packed iteration counts (movaps target)

alignb 4
font_save:                 ; VGA text font (plane 2, 8 KB)
    resb 8192

dac_save:                  ; text-mode DAC palette (256*RGB)
    resb 768

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
