; bounce - a ball that bounces around the screen.
;   Self-backgrounds via sys_detach, so the prompt comes straight back and the
;   ball keeps moving. It loops forever; the only way to stop it is to find its
;   PID with `ps` and `kill <pid>`.
[bits 32]
[org 0x00000000]

VGA   equ 0xC00B8000
COLS  equ 80
XMIN  equ 0
XMAX  equ 79
YMIN  equ 2            ; don't hurt my "nice" banner ;-)
YMAX  equ 24
DELAY equ 3            ; ticks between frames 
BALL  equ 0x0F07       ; attribute 0x0F (white) | char 0x07 (bullet)

_start:
    call .base
.base:
    pop ebp
    sub ebp, .base

    mov eax,43             ; sys_detach -> shell returns to the prompt
    int 0x80

    call save              ; remember it
    call draw              
.frame:
    call restore           ; what did you remember?
    call update            
    call save              
    call draw            
    call pace              ; wait a few ticks 
    jmp .frame             ; stop by ps and kill

draw:
    call cell_ptr
    mov word [edi],BALL
    ret
save:
    call cell_ptr
    mov ax,[edi]           ; char+attribute
    mov [ebp+saved],ax
    ret
restore:
    call cell_ptr
    mov ax,[ebp+saved]     
    mov word [edi],ax
    ret

;  edi = VGA+(ypos*COLS + xpos)*2
cell_ptr:
    push eax
    push ebx
    movzx eax,byte [ebp+ypos]
    imul eax,COLS
    movzx ebx,byte [ebp+xpos]
    add eax,ebx
    shl eax,1
    add eax,VGA
    mov edi,eax
    pop ebx
    pop eax
    ret

; update position
update:
    push eax
    push ebx
    ; --- X axis ---
    movzx eax,byte [ebp+xpos]
    movsx ebx,byte [ebp+dltx]
    add eax,ebx
    cmp eax,XMAX
    jg .x_hi
    cmp eax,XMIN
    jl .x_lo
    jmp .x_store
.x_hi:
    mov eax,XMAX
    neg byte [ebp+dltx]
    jmp .x_store
.x_lo:
    mov eax,XMIN
    neg byte [ebp+dltx]
.x_store:
    mov [ebp+xpos],al
    ; --- Y axis ---
    movzx eax,byte [ebp+ypos]
    movsx ebx,byte [ebp+dlty]
    add eax,ebx
    cmp eax,YMAX
    jg .y_hi
    cmp eax,YMIN
    jl .y_lo
    jmp .y_store
.y_hi:
    mov eax,YMAX
    neg byte [ebp+dlty]
    jmp .y_store
.y_lo:
    mov eax,YMIN
    neg byte [ebp+dlty]
.y_store:
    mov [ebp+ypos],al
    pop ebx
    pop eax
    ret

;  wait DELAY 
pace:
    push eax
    push edx
    mov eax,8              ; sys_get_tick
    int 0x80
    add eax,DELAY
    mov edx,eax            ; target tick (edx survives int 0x80)
.wait:
    hlt
    mov eax,8
    int 0x80
    cmp eax,edx
    jb .wait
    pop edx
    pop eax
    ret

xpos  db 40
ypos  db 12
dltx  db 1
dlty  db 1
saved dw 0                 ; char+attr snapshot under the ball
