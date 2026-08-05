; breakout - a Breakout/Arkanoid game in VGA text mode.
;   Runs in the foreground (does NOT detach), so while it runs the shell is
;   parked in exec_bin's wait loop: no banner redraw, no key stealing, the whole
;   screen and keyboard are ours. On game over / quit we clear and `ret`, and the
;   shell brings the prompt (and banner) back.
;
;   Controls: Left/Right arrows (or A/D) move the paddle. Q quits.
;   5 rows of bricks replace the banner; each brick is erased (blanked) when the
;   ball hits it. Clear all 5 rows and a fresh wall appears. Miss the ball at the
;   bottom and it's game over.
[bits 32]
[org 0x00000000]

VGA      equ 0xC00B8000
COLS     equ 80
TOPROW   equ 0                     ; ceiling the ball bounces off
BRK_TOP  equ 1                     ; first brick row
BRK_ROWS equ 5
BRK_BOT  equ BRK_TOP+BRK_ROWS-1    ; last brick row (5)
PADROW   equ 24                    ; paddle row (bottom)
PADW     equ 8                     ; paddle width
NBRICKS  equ BRK_ROWS*COLS         ; bricks in a full wall (5*80 = 400)

BALLCH   equ 0x0F07                ; white bullet
PADCH    equ 0x0EDB                ; yellow solid block
SPACE    equ 0x0720                ; blank cell
BRKCH    equ 0xDB                  ; brick glyph (used for collision test)

KEY_LEFT  equ 0x80                 ; sentinels returned by sys_get_key (7)
KEY_RIGHT equ 0x81

DELAY    equ 3                     ; ticks between frames (PIT = 100 Hz)
PSTEP    equ 4                     ; paddle cells moved per key event
BALL_DIV equ 2                     ; advance the ball once per this many frames

_start:
    call .base
.base:
    pop ebp
    sub ebp,.base

    mov eax,4                      ; sys_cls -> wipe the banner and screen
    int 0x80

    call init_bricks
    call draw_paddle

.loop:
    ; paddle: read + redraw every frame so it stays responsive
    call erase_paddle
    call read_keys                 ; move paddle, maybe set fquit
    call draw_paddle
    cmp byte [ebp+fquit],0
    jne .quit

    ; ball: advance only once per BALL_DIV frames (slower than the paddle)
    inc byte [ebp+framec]
    cmp byte [ebp+framec],BALL_DIV
    jb .nomove
    mov byte [ebp+framec],0
    call erase_ball
    call step_ball                 ; move + collisions, maybe set fover
    cmp byte [ebp+fover],0
    jne .gameover
    call draw_ball
.nomove:
    call pace
    jmp .loop

.gameover:
    call show_gameover
.quit:
    mov eax,4                      ; sys_cls -> back to a clean text screen
    int 0x80
    ret                            ; -> proc_exit; shell redraws prompt + banner

; ---------------------------------------------------------------
; cell_ptr: in eax = x, ecx = y -> edi = VGA cell address.
;   Preserves eax/ecx/edx.
; ---------------------------------------------------------------
cell_ptr:
    push edx
    mov edx,ecx
    imul edx,COLS
    add edx,eax
    shl edx,1
    lea edi,[VGA+edx]
    pop edx
    ret

; --- fill 5 rows of coloured bricks, reset the counter ---
init_bricks:
    pushad
    mov dword [ebp+nbrick],NBRICKS
    xor ebx,ebx                    ; row index 0..BRK_ROWS-1
.rows:
    movzx eax,byte [ebp+rowcolors+ebx]  ; attribute
    shl ax,8
    mov al,BRKCH                   ; ax = attr:char
    lea ecx,[ebx+BRK_TOP]          ; y = BRK_TOP + row
    push eax
    mov eax,0
    call cell_ptr                  ; edi -> start of row (x=0)
    pop eax
    mov ecx,COLS
.cols:
    mov [edi],ax
    add edi,2
    dec ecx
    jnz .cols
    inc ebx
    cmp ebx,BRK_ROWS
    jb .rows
    popad
    ret

maybe_refill:
    cmp dword [ebp+nbrick],0
    jne .done
    call init_bricks
.done:
    ret

; --- ball ---
draw_ball:
    pushad
    movzx eax,byte [ebp+ballx]
    movzx ecx,byte [ebp+bally]
    call cell_ptr
    mov word [edi],BALLCH
    popad
    ret
erase_ball:
    pushad
    movzx eax,byte [ebp+ballx]
    movzx ecx,byte [ebp+bally]
    call cell_ptr
    mov word [edi],SPACE
    popad
    ret

; --- paddle (PADW cells at padx, row PADROW) ---
draw_paddle:
    pushad
    movzx eax,byte [ebp+padx]
    mov ecx,PADROW
    call cell_ptr
    mov ecx,PADW
.d:
    mov word [edi],PADCH
    add edi,2
    dec ecx
    jnz .d
    popad
    ret
erase_paddle:
    pushad
    movzx eax,byte [ebp+padx]
    mov ecx,PADROW
    call cell_ptr
    mov ecx,PADW
.e:
    mov word [edi],SPACE
    add edi,2
    dec ecx
    jnz .e
    popad
    ret

; --- drain the key buffer; move the paddle; Q sets fquit ---
read_keys:
    pushad
.rk:
    mov eax,7                      ; sys_get_key (0 if none)
    int 0x80
    test al,al
    jz .done
    cmp al,KEY_LEFT
    je .left
    cmp al,'a'
    je .left
    cmp al,KEY_RIGHT
    je .right
    cmp al,'d'
    je .right
    cmp al,'q'
    je .q
    cmp al,'Q'
    je .q
    jmp .rk
.left:
    movzx eax,byte [ebp+padx]
    sub eax,PSTEP
    jns .lset                      ; clamp at the left wall (0)
    xor eax,eax
.lset:
    mov [ebp+padx],al
    jmp .rk
.right:
    movzx eax,byte [ebp+padx]
    add eax,PSTEP
    cmp eax,COLS-PADW              ; clamp at the right wall (76)
    jbe .rset
    mov eax,COLS-PADW
.rset:
    mov [ebp+padx],al
    jmp .rk
.q:
    mov byte [ebp+fquit],1
    jmp .rk
.done:
    popad
    ret

; --- advance the ball, reflecting off walls/paddle/bricks ---
step_ball:
    pushad
    ; horizontal: newx = ballx + vx, reflect at side walls (stay in place)
    movzx eax,byte [ebp+ballx]
    movsx ebx,byte [ebp+vx]
    add eax,ebx
    cmp eax,0
    jl .xref
    cmp eax,COLS-1
    jg .xref
    jmp .xset
.xref:
    neg byte [ebp+vx]
    movzx eax,byte [ebp+ballx]
.xset:
    mov [ebp+newx],al

    ; vertical: newy = bally + vy
    movzx ecx,byte [ebp+bally]
    movsx ebx,byte [ebp+vy]
    add ecx,ebx
    cmp ecx,TOPROW
    jl .topref
    cmp ecx,PADROW
    jge .padchk
    jmp .yset
.topref:
    neg byte [ebp+vy]
    movzx ecx,byte [ebp+bally]
    jmp .yset
.padchk:
    ; ball reached the paddle row — does the paddle catch it?
    movzx eax,byte [ebp+newx]
    movzx edx,byte [ebp+padx]
    cmp eax,edx
    jl .miss
    add edx,PADW-1
    cmp eax,edx
    jg .miss
    neg byte [ebp+vy]              ; bounce back up
    movzx ecx,byte [ebp+bally]
    jmp .yset
.miss:
    mov byte [ebp+fover],1
    jmp .out
.yset:
    mov [ebp+newy],cl

    ; --- vertical brick hit: only if the row actually changed ---
    mov al,[ebp+bally]
    cmp al,cl
    je .vno
    movzx ecx,byte [ebp+newy]
    cmp ecx,BRK_TOP
    jl .vno
    cmp ecx,BRK_BOT
    jg .vno
    movzx eax,byte [ebp+newx]
    call cell_ptr
    cmp byte [edi],BRKCH
    jne .vno
    mov word [edi],SPACE          ; erase the brick
    dec dword [ebp+nbrick]
    neg byte [ebp+vy]
    movzx ecx,byte [ebp+bally]    ; don't move into it
    mov [ebp+newy],cl
    call maybe_refill
.vno:
    ; --- horizontal brick hit at (newx, bally): only if column changed ---
    mov al,[ebp+ballx]
    mov cl,[ebp+newx]
    cmp al,cl
    je .hno
    movzx ecx,byte [ebp+bally]
    cmp ecx,BRK_TOP
    jl .hno
    cmp ecx,BRK_BOT
    jg .hno
    movzx eax,byte [ebp+newx]
    call cell_ptr
    cmp byte [edi],BRKCH
    jne .hno
    mov word [edi],SPACE
    dec dword [ebp+nbrick]
    neg byte [ebp+vx]
    movzx eax,byte [ebp+ballx]    ; don't move into it
    mov [ebp+newx],al
    call maybe_refill
.hno:
    ; commit new position
    mov al,[ebp+newx]
    mov [ebp+ballx],al
    mov al,[ebp+newy]
    mov [ebp+bally],al
.out:
    popad
    ret

; --- pace: wait DELAY ticks, hlt-ing so we yield the CPU ---
pace:
    push eax
    push edx
    mov eax,8                      ; sys_get_tick
    int 0x80
    add eax,DELAY
    mov edx,eax
.w:
    hlt
    mov eax,8
    int 0x80
    cmp eax,edx
    jb .w
    pop edx
    pop eax
    ret

; --- flash "GAME OVER" for ~1.5 s ---
show_gameover:
    pushad
    mov eax,(COLS-9)/2             ; centre column
    mov ecx,12
    call cell_ptr
    lea esi,[ebp+gomsg]
.p:
    mov al,[esi]
    test al,al
    jz .wait
    mov ah,0x0C                    ; bright red
    mov [edi],ax
    add edi,2
    inc esi
    jmp .p
.wait:
    mov eax,8
    int 0x80
    add eax,150                    ; ~1.5 s at 100 Hz
    mov edx,eax
.w:
    hlt
    mov eax,8
    int 0x80
    cmp eax,edx
    jb .w
    popad
    ret

; --- state (mutable — lives in the loaded, writable image) ---
ballx     db 40
bally     db 22
vx        db 1
vy        db -1                    ; start heading up
padx      db 38
newx      db 0
newy      db 0
fquit     db 0
fover     db 0
framec    db 0                     ; frame counter for ball-speed division
nbrick    dd 0
rowcolors db 0x0C,0x0E,0x0A,0x0B,0x0D   ; red, yellow, green, cyan, magenta
gomsg     db "GAME OVER",0
