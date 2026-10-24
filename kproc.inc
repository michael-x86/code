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

