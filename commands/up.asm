[bits 32]
[org 0x00000000]

_start:
    ; --- Calculate our Dynamic Base Offset ---
    call .get_base
.get_base:
    pop ebp                 
    sub ebp, .get_base    

    ; --- Print "uptime: " ---
    mov eax,1              
    lea esi,[ebp+uptime_msg]
    int 0x80

    ; --- Get current ticks ---
    mov eax,8              ; sys_get_tick -> returns value in eax
    int 0x80

    ; --- Calculate total seconds ---
    mov ebx,100            ; assume ticks are at 100 Hz
    xor edx,edx
    div ebx                 ; eax = total seconds

    ; --- Calculate & Print Hours ---
    mov ebx,3600
    xor edx,edx
    div ebx                 ; eax = hours, edx = remainder seconds
    push edx                ; save remainder seconds for later

    mov ebx,eax            ; ebx = hours value for sys_print_int
    mov eax,6              ; sys_print_int
    int 0x80

    mov eax,1              ; sys_print
    lea esi,[ebp+uptime_sep]
    int 0x80

    ; --- Calculate & Print Minutes ---
    pop eax                 ; restore remainder seconds
    mov ebx,60
    xor edx,edx
    div ebx                 ; eax = minutes, edx = seconds
    push edx                ; save seconds for later

    mov ebx,eax            ; ebx = minutes value for sys_print_int
    mov eax,6              ; sys_print_int
    int 0x80

    mov eax,1              ; sys_print
    lea esi,[ebp+uptime_sep]
    int 0x80

    ; --- Print Seconds ---
    pop ebx                 ; restore seconds directly into ebx
    mov eax,6              ; sys_print_int
    int 0x80

    ; --- Print Newline ---
    mov eax,3              ; sys_newline
    int 0x80

    ret

uptime_msg db "uptime: ",0
uptime_sep db ":",0
