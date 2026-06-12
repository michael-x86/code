[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base        

    ; --- sys_get_ps_info ---
    lea ebx,[ebp+ps_buffer]  ; local scratch buffer
    mov eax,22             
    int 0x80
    cmp eax,-1          
    je .done

    ; --- System Header ---
    lea esi,[ebp+hdr_msg]
    mov eax,2           
    int 0x80

    ; ---  Task 0 ---
    lea esi,[ebp+task0_lbl]   ; Skynet
    mov eax,2                  
    int 0x80
    mov ebx,[ebp+ps_buffer+4] 
    mov eax,5                
    int 0x80
    lea esi, [ebp+idle_lbl]   ; Task 0 is always architecturally IDLE
    mov eax,2                  
    int 0x80

    ; ---  Task 1 ---
    lea esi,[ebp+task1_lbl]   ; HAL-9000
    mov eax,2                  
    int 0x80
    mov ebx,[ebp+ps_buffer+8] 
    mov eax,5                  
    int 0x80
    mov ecx, 1
    call print_user_state

    ; ---  Task 2 ---
    lea esi,[ebp+task2_lbl]   ; /sbin/init
    mov eax,2                  
    int 0x80
    mov ebx,[ebp+ps_buffer+12] 
    mov eax,5                  
    int 0x80
    mov ecx,2
    call print_user_state

    ; ---  Fake Task  ---
    lea esi,[ebp+pstsk_lbl]   ; ps
    mov eax,2                  
    int 0x80
    mov ebx,0xc0421000 
    mov eax,5                  
    int 0x80
    mov ecx,2
    call print_user_state
.done:
    ret

print_user_state:
    mov edx,[ebp+ps_buffer+0] ; Extract from kernel snapshot
    cmp edx,ecx
    je .running
    lea esi,[ebp+sleeping_lbl]
    mov eax,2                  
    int 0x80
    ret
.running:
    lea esi,[ebp+running_lbl]
    mov eax,2                  
    int 0x80
    ret

hdr_msg:      db "PID   TASK  NAME          ADDRESS      STATE",13,0
task0_lbl:    db "root  000   skynet -d     0x", 0
task1_lbl:    db "root  001   [hal-9000]    0x", 0
task2_lbl:    db "root  002   /sbin/init    0x", 0
pstsk_lbl:    db "root  003   ps            0x", 0

idle_lbl:     db "   IDLE", 13, 0
sleeping_lbl: db "   SLEEPING", 13, 0
running_lbl:  db "   RUNNING", 13, 0

section .bss
alignb 4
ps_buffer:    resd 4       ; 16 bytes =  kernel struct payload
