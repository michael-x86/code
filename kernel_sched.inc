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
