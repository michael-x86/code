; --------------------
; in:               
;   esi -> string   
; out:              
;   edx integer     
; --------------------
asc2int:      
    xor edx,edx
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
;   ebx: integer    
; -------------------
hex2int:
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

;-------------------
; out:     
;  physical address
;-------------------
virt2phys:
    push ebx
    push ecx
    push edx
    mov edx,eax
    mov ebx,eax
    shr ebx,22
    cmp ebx,768
    je .pt0
    cmp ebx,769
    je .pt1
    clc
    stc
    jmp .fail
.pt0:
    mov ecx,V2P(first_page_table)
    jmp .walk
.pt1:
    mov ecx,V2P(second_page_table)
.walk:
    mov ebx,edx
    shr ebx,12
    and ebx,03FFh
    mov eax,[ecx+ebx*4]
    test eax,1
    jz .not_present
    and eax,0FFFFF000h
    mov ebx,edx
    and ebx,0FFFh
    add eax,ebx
    clc
    jmp .done
.not_present:
    stc
.done:
.fail:
    pop edx
    pop ecx
    pop ebx
    ret
