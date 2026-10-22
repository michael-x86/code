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

