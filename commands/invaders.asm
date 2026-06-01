; =============================================================================
; invaders — Space Invaders text-mode game for x86 Assembly Kernel
; =============================================================================
; Controls: A/D or arrows = move, Space = fire, Q = quit
; Aliens 5x11 grid. Barriers for cover. Text-mode VGA.
; =============================================================================

[bits 32]
[org 0x00000000]

%define W     80
%define H     25
%define VGA   0xC00B8000
%define NROWS 5
%define NCOLS 11
%define NALI  (NROWS * NCOLS)
%define NBAR  4
%define MAXPB 4
%define MAXAB 3

_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

    mov eax, 4              ; cls
    int 0x80

    call init_game

; ── Main loop ─────────────────────────────────────────────────────────────────
.loop:
    ; wait for new tick
    mov eax, 8
    int 0x80
    mov [ebp + vtick], eax
.tick:
    mov eax, 8
    int 0x80
    cmp eax, [ebp + vtick]
    je .tick

    inc dword [ebp + vframe]

    ; player death cooldown
    cmp dword [ebp + vdead], 0
    je .not_decdead
    dec dword [ebp + vdead]
.not_decdead:

    call do_input
    call move_pbullets
    call move_abullets

    ; alien movement every 4 frames
    mov eax, [ebp + vframe]
    and eax, 3
    jnz .skip_amove
    call move_aliens
.skip_amove:

    ; alien fire every 24 frames
    mov eax, [ebp + vframe]
    and eax, 23
    jnz .skip_afire
    call alien_fire
.skip_afire:

    call do_collisions
    call check_end
    cmp dword [ebp + vdone], 0
    jne .finish

    call render
    jmp .loop

.finish:
    call render
    lea esi, [ebp + msg_over]
    mov eax, 2
    int 0x80
    mov eax, 3
    int 0x80
    ret

; ══════════════════════════════════════════════════════════════════════════════
; INIT
; ══════════════════════════════════════════════════════════════════════════════
init_game:
    pushad
    mov dword [ebp + vpx], 39
    mov dword [ebp + vlives], 3
    mov dword [ebp + vscore], 0
    mov dword [ebp + vadir], 1
    mov dword [ebp + vabx], 4
    mov dword [ebp + vaby], 3
    mov dword [ebp + vframe], 0
    mov dword [ebp + vdone], 0
    mov dword [ebp + vpbct], 0
    mov dword [ebp + vabct], 0
    mov dword [ebp + vdead], 0

    xor ecx, ecx
.cl_lp:
    cmp ecx, NALI
    jge .cl_bar
    mov byte [ebp + valive + ecx], 1
    inc ecx
    jmp .cl_lp

.cl_bar:
    mov dword [ebp + vbx + 0], 12
    mov dword [ebp + vbx + 4], 28
    mov dword [ebp + vbx + 8], 46
    mov dword [ebp + vbx + 12], 64
    xor ecx, ecx
.cl_bp:
    cmp ecx, NBAR
    jge .cl_done
    mov dword [ebp + vbhp + ecx * 4], 3
    inc ecx
    jmp .cl_bp
.cl_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; INPUT
; ══════════════════════════════════════════════════════════════════════════════
do_input:
    pushad
    mov eax, 7              ; get_key
    int 0x80
    test al, al
    jz .di_done

    cmp al, 'q'
    je .di_q
    cmp al, 'Q'
    je .di_q
    cmp al, 'a'
    je .di_l
    cmp al, 'A'
    je .di_l
    cmp al, 0x4B
    je .di_l
    cmp al, 'd'
    je .di_r
    cmp al, 'D'
    je .di_r
    cmp al, 0x4D
    je .di_r
    cmp al, ' '
    je .di_f
    jmp .di_done

.di_q:
    mov dword [ebp + vdone], 1
    jmp .di_done

.di_l:
    cmp dword [ebp + vdead], 0
    jne .di_done
    cmp dword [ebp + vpx], 2
    jle .di_done
    dec dword [ebp + vpx]
    jmp .di_done

.di_r:
    cmp dword [ebp + vdead], 0
    jne .di_done
    cmp dword [ebp + vpx], W - 3
    jge .di_done
    inc dword [ebp + vpx]
    jmp .di_done

.di_f:
    cmp dword [ebp + vpbct], MAXPB
    jge .di_done
    mov ecx, [ebp + vpbct]
    mov eax, [ebp + vpx]
    mov [ebp + vpbx + ecx * 4], eax
    mov dword [ebp + vpby + ecx * 4], H - 3
    inc dword [ebp + vpbct]
    jmp .di_done

.di_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; MOVE PLAYER BULLETS
; ══════════════════════════════════════════════════════════════════════════════
move_pbullets:
    pushad
    xor ecx, ecx
.pb_lp:
    cmp ecx, [ebp + vpbct]
    jge .pb_done
    dec dword [ebp + vpby + ecx * 4]
    cmp dword [ebp + vpby + ecx * 4], 1
    jge .pb_nxt

    ; remove bullet[ecx] by shifting subsequent bullets down
    mov edx, ecx
.pb_sh:
    inc edx
    cmp edx, [ebp + vpbct]
    jge .pb_rm
    mov eax, [ebp + vpbx + edx * 4]
    mov [ebp + vpbx + edx * 4 - 4], eax
    mov eax, [ebp + vpby + edx * 4]
    mov [ebp + vpby + edx * 4 - 4], eax
    jmp .pb_sh
.pb_rm:
    dec dword [ebp + vpbct]
    jmp .pb_lp           ; don't inc idx

.pb_nxt:
    inc ecx
    jmp .pb_lp
.pb_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; MOVE ALIEN BULLETS
; ══════════════════════════════════════════════════════════════════════════════
move_abullets:
    pushad
    xor ecx, ecx
.ab_lp:
    cmp ecx, [ebp + vabct]
    jge .ab_done
    inc dword [ebp + vaby_d + ecx * 4]
    cmp dword [ebp + vaby_d + ecx * 4], H - 1
    jl .ab_nxt
    mov edx, ecx
.ab_sh:
    inc edx
    cmp edx, [ebp + vabct]
    jge .ab_rm
    mov eax, [ebp + vabx_d + edx * 4]
    mov [ebp + vabx_d + edx * 4 - 4], eax
    mov eax, [ebp + vaby_d + edx * 4]
    mov [ebp + vaby_d + edx * 4 - 4], eax
    jmp .ab_sh
.ab_rm:
    dec dword [ebp + vabct]
    jmp .ab_lp
.ab_nxt:
    inc ecx
    jmp .ab_lp
.ab_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; MOVE ALIENS
; ══════════════════════════════════════════════════════════════════════════════
move_aliens:
    pushad
    mov eax, [ebp + vadir]
    cmp eax, 1
    jne .ma_rev

    ; moving right
    mov eax, [ebp + vabx]
    add eax, NCOLS * 2
    cmp eax, W - 4
    jl .ma_go_r
    neg dword [ebp + vadir]
    inc dword [ebp + vaby]
    jmp .ma_done
.ma_go_r:
    inc dword [ebp + vabx]
    jmp .ma_done

.ma_rev:
    mov eax, [ebp + vabx]
    cmp eax, 2
    jg .ma_go_l
    neg dword [ebp + vadir]
    inc dword [ebp + vaby]
    jmp .ma_done
.ma_go_l:
    dec dword [ebp + vabx]
.ma_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; ALIEN FIRE
; ══════════════════════════════════════════════════════════════════════════════
alien_fire:
    pushad
    cmp dword [ebp + vabct], MAXAB
    jge .af_done

    ; pick column = frame % NCOLS
    mov eax, [ebp + vframe]
    xor edx, edx
    mov ecx, NCOLS
    div ecx
    mov edi, edx            ; edi = column

    ; find lowest alive in this column
    mov ecx, NROWS - 1
.af_lp:
    cmp ecx, 0
    jl .af_done
    imul edx, ecx, NCOLS
    add edx, edi
    cmp byte [ebp + valive + edx], 0
    je .af_nxt

    ; fire
    push eax
    mov eax, edi
    shl eax, 1
    add eax, [ebp + vabx]   ; bullet x
    mov ebx, ecx
    shl ebx, 1
    add ebx, [ebp + vaby]
    inc ebx                 ; bullet y
    mov ecx, [ebp + vabct]
    mov [ebp + vabx_d + ecx * 4], eax
    mov [ebp + vaby_d + ecx * 4], ebx
    inc dword [ebp + vabct]
    pop eax
    jmp .af_done

.af_nxt:
    dec ecx
    jmp .af_lp
.af_done:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; COLLISIONS
; ══════════════════════════════════════════════════════════════════════════════
do_collisions:
    pushad

    ; -- player bullets vs aliens --
    xor ecx, ecx
.dc_pb:
    cmp ecx, [ebp + vpbct]
    jge .dc_pbar

    mov eax, [ebp + vpbx + ecx * 4]     ; bullet x
    mov ebx, [ebp + vpby + ecx * 4]     ; bullet y

    xor edi, edi
.dc_ab:
    cmp edi, NALI
    jge .dc_pbnxt
    cmp byte [ebp + valive + edi], 0
    je .dc_abnxt

    ; alien screen position
    push edx
    push eax
    mov eax, edi
    xor edx, edx
    mov esi, NCOLS
    div esi                 ; eax=row, edx=col
    push eax
    mov esi, edx
    shl esi, 1
    add esi, [ebp + vabx]   ; screen col
    pop eax
    shl eax, 1
    add eax, [ebp + vaby]   ; screen y

    pop edx                 ; bullet x into edx (was eax)
    cmp edx, esi            ; x match?
    jne .dc_pop
    cmp ebx, eax            ; y match?
    jne .dc_pop

    ; HIT
    mov byte [ebp + valive + edi], 0

    ; score by row
    push ecx
    mov eax, edi
    xor edx, edx
    mov ecx, NCOLS
    div ecx
    cmp eax, 1
    jle .dc_top
    cmp eax, 3
    jle .dc_mid
    add dword [ebp + vscore], 10
    jmp .dc_scdone
.dc_top:
    add dword [ebp + vscore], 30
    jmp .dc_scdone
.dc_mid:
    add dword [ebp + vscore], 20
.dc_scdone:
    pop ecx
    pop edx

    ; remove bullet
    push edi
    call _rm_pb
    pop edi
    jmp .dc_pbnxt

.dc_pop:
    pop edx
.dc_abnxt:
    inc edi
    jmp .dc_ab

.dc_pbnxt:
    inc ecx
    jmp .dc_pb

    ; -- player bullets vs barriers --
.dc_pbar:
    xor ecx, ecx
.dc_bar:
    cmp ecx, [ebp + vpbct]
    jge .dc_abul

    mov eax, [ebp + vpbx + ecx * 4]
    mov ebx, [ebp + vpby + ecx * 4]
    cmp ebx, H - 5
    jne .dc_bnxt

    xor edi, edi
.dc_blp2:
    cmp edi, NBAR
    jge .dc_bnxt
    cmp dword [ebp + vbhp + edi * 4], 0
    je .dc_bsnxt

    mov edx, [ebp + vbx + edi * 4]
    cmp eax, edx
    jl .dc_bsnxt
    add edx, 3
    cmp eax, edx
    jg .dc_bsnxt

    dec dword [ebp + vbhp + edi * 4]
    call _rm_pb
    jmp .dc_bnxt

.dc_bsnxt:
    inc edi
    jmp .dc_blp2
.dc_bnxt:
    inc ecx
    jmp .dc_bar

    ; -- alien bullets vs player --
.dc_abul:
    xor ecx, ecx
.dc_ap:
    cmp ecx, [ebp + vabct]
    jge .dc_abar

    cmp dword [ebp + vdead], 0
    jne .dc_apnxt

    mov eax, [ebp + vabx_d + ecx * 4]
    mov ebx, [ebp + vaby_d + ecx * 4]
    cmp ebx, H - 2
    jne .dc_apnxt

    mov edx, [ebp + vpx]
    cmp eax, edx
    jl .dc_apnxt
    add edx, 1
    cmp eax, edx
    jg .dc_apnxt

    dec dword [ebp + vlives]
    mov dword [ebp + vdead], 30
    call _rm_ab
    cmp dword [ebp + vlives], 0
    jg .dc_apnxt
    mov dword [ebp + vdone], 1
    popad
    ret

.dc_apnxt:
    inc ecx
    jmp .dc_ap

    ; -- alien bullets vs barriers --
.dc_abar:
    xor ecx, ecx
.dc_abl2:
    cmp ecx, [ebp + vabct]
    jge .dc_done

    mov eax, [ebp + vabx_d + ecx * 4]
    mov ebx, [ebp + vaby_d + ecx * 4]
    cmp ebx, H - 5
    jne .dc_abnxt2

    xor edi, edi
.dc_ablp:
    cmp edi, NBAR
    jge .dc_abnxt2
    cmp dword [ebp + vbhp + edi * 4], 0
    je .dc_absnxt

    mov edx, [ebp + vbx + edi * 4]
    cmp eax, edx
    jl .dc_absnxt
    add edx, 3
    cmp eax, edx
    jg .dc_absnxt

    dec dword [ebp + vbhp + edi * 4]
    call _rm_ab
    jmp .dc_abnxt2

.dc_absnxt:
    inc edi
    jmp .dc_ablp
.dc_abnxt2:
    inc ecx
    jmp .dc_abl2

.dc_done:
    popad
    ret

; ── helper: remove player bullet at index ecx ─────────────────────────────────
_rm_pb:
    pushad
    mov edx, ecx
.rmpb_lp:
    inc edx
    cmp edx, [ebp + vpbct]
    jge .rmpb_rm
    mov eax, [ebp + vpbx + edx * 4]
    mov [ebp + vpbx + edx * 4 - 4], eax
    mov eax, [ebp + vpby + edx * 4]
    mov [ebp + vpby + edx * 4 - 4], eax
    jmp .rmpb_lp
.rmpb_rm:
    dec dword [ebp + vpbct]
    popad
    ret

; ── helper: remove alien bullet at index ecx ──────────────────────────────────
_rm_ab:
    pushad
    mov edx, ecx
.rmab_lp:
    inc edx
    cmp edx, [ebp + vabct]
    jge .rmab_rm
    mov eax, [ebp + vabx_d + edx * 4]
    mov [ebp + vabx_d + edx * 4 - 4], eax
    mov eax, [ebp + vaby_d + edx * 4]
    mov [ebp + vaby_d + edx * 4 - 4], eax
    jmp .rmab_lp
.rmab_rm:
    dec dword [ebp + vabct]
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; CHECK END
; ══════════════════════════════════════════════════════════════════════════════
check_end:
    pushad

    ; aliens reached player?
    mov eax, [ebp + vaby]
    add eax, NROWS * 2
    cmp eax, H - 3
    jl .ce_alive
    mov dword [ebp + vdone], 1
    popad
    ret

.ce_alive:
    ; all dead?
    xor ecx, ecx
.ce_lp:
    cmp ecx, NALI
    jge .ce_win
    cmp byte [ebp + valive + ecx], 1
    je .ce_still
    inc ecx
    jmp .ce_lp

.ce_win:
    mov dword [ebp + vdone], 1
    popad
    ret

.ce_still:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; RENDER
; ══════════════════════════════════════════════════════════════════════════════
render:
    pushad

    ; clear buffer (green space)
    mov edi, VGA
    mov ecx, W * H
    mov ax, 0x0A20
    rep stosw

    ; -- HUD row 0 --
    ; "SCORE:" at col 0
    mov byte [VGA + 0], 'S'
    mov byte [VGA + 2], 'C'
    mov byte [VGA + 4], 'O'
    mov byte [VGA + 6], 'R'
    mov byte [VGA + 8], 'E'
    mov byte [VGA + 10], ':'

    ; score at col 6..11
    mov eax, [ebp + vscore]
    mov edi, 11             ; rightmost digit col
    mov ecx, 6              ; up to 6 digits
.dn_lp:
    push eax
    xor edx, edx
    mov ebx, 10
    div ebx
    add dl, '0'
    mov dh, 0x0A
    push eax
    mov eax, edi
    shl eax, 1
    add eax, VGA
    mov [eax], dx
    pop eax
    dec edi
    pop eax
    dec ecx
    jnz .dn_lp

    ; "LIVES:" at col 66
    mov byte [VGA + 132], 'L'
    mov byte [VGA + 134], 'I'
    mov byte [VGA + 136], 'V'
    mov byte [VGA + 138], 'E'
    mov byte [VGA + 140], 'S'
    mov byte [VGA + 142], ':'
    movzx eax, byte [ebp + vlives]
    add al, '0'
    mov ah, 0x0A
    mov [VGA + 144], ax

    ; -- aliens --
    xor ecx, ecx
.dr_al:
    cmp ecx, NALI
    jge .dr_bar
    cmp byte [ebp + valive + ecx], 0
    je .dr_anxt

    push edx
    mov eax, ecx
    xor edx, edx
    mov ebx, NCOLS
    div ebx                 ; eax=row, edx=col

    push eax
    mov esi, edx
    shl esi, 1
    add esi, [ebp + vabx]   ; screen col
    pop eax
    shl eax, 1
    add eax, [ebp + vaby]   ; screen row

    ; char + color by relative row
    mov edx, eax
    sub edx, [ebp + vaby]
    cmp edx, 2
    jl .dr_top
    cmp edx, 4
    jl .dr_mid

    mov dl, '*'
    mov dh, 0x0C            ; light red
    jmp .dr_put

.dr_top:
    mov dl, 0x5E            ; ^ caret
    mov dh, 0x0A            ; light green
    jmp .dr_put

.dr_mid:
    mov dl, '@'
    mov dh, 0x0E            ; yellow
    jmp .dr_put

.dr_put:
    imul eax, W
    add eax, esi
    shl eax, 1
    add eax, VGA
    mov [eax], dx

    pop edx
.dr_anxt:
    inc ecx
    jmp .dr_al

    ; -- barriers --
.dr_bar:
    xor ecx, ecx
.dr_blp:
    cmp ecx, NBAR
    jge .dr_ply
    cmp dword [ebp + vbhp + ecx * 4], 0
    je .dr_bsnxt

    mov edx, [ebp + vbx + ecx * 4]
    mov eax, H - 5
    imul eax, W
    add eax, edx
    shl eax, 1
    add eax, VGA

    mov bl, 0x02            ; green
    cmp dword [ebp + vbhp + ecx * 4], 2
    jge .dr_bcol
    mov bl, 0x06
    cmp dword [ebp + vbhp + ecx * 4], 1
    jge .dr_bcol
    mov bl, 0x04
.dr_bcol:
    mov byte [eax], '#'
    mov byte [eax + 1], bl
    mov byte [eax + 2], '#'
    mov byte [eax + 3], bl
    mov byte [eax + 4], '#'
    mov byte [eax + 5], bl
    mov byte [eax + 6], '#'
    mov byte [eax + 7], bl

.dr_bsnxt:
    inc ecx
    jmp .dr_blp

    ; -- player --
.dr_ply:
    cmp dword [ebp + vdead], 0
    jne .dr_pbul
    mov eax, H - 2
    imul eax, W
    add eax, [ebp + vpx]
    shl eax, 1
    add eax, VGA
    mov byte [eax], 'T'
    mov byte [eax + 1], 0x0F
    mov byte [eax + 2], '/'
    mov byte [eax + 3], 0x0F

    ; -- player bullets --
.dr_pbul:
    xor ecx, ecx
.dr_pbl:
    cmp ecx, [ebp + vpbct]
    jge .dr_abul
    mov eax, [ebp + vpby + ecx * 4]
    cmp eax, 1
    jl .dr_pbnxt
    imul eax, W
    add eax, [ebp + vpbx + ecx * 4]
    shl eax, 1
    add eax, VGA
    mov byte [eax], '|'
    mov byte [eax + 1], 0x0B
.dr_pbnxt:
    inc ecx
    jmp .dr_pbl

    ; -- alien bullets --
.dr_abul:
    xor ecx, ecx
.dr_abl:
    cmp ecx, [ebp + vabct]
    jge .dr_exit
    mov eax, [ebp + vaby_d + ecx * 4]
    cmp eax, 1
    jl .dr_abnxt
    imul eax, W
    add eax, [ebp + vabx_d + ecx * 4]
    shl eax, 1
    add eax, VGA
    mov byte [eax], '!'
    mov byte [eax + 1], 0x0C
.dr_abnxt:
    inc ecx
    jmp .dr_abl

.dr_exit:
    popad
    ret

; ══════════════════════════════════════════════════════════════════════════════
; DATA
; ══════════════════════════════════════════════════════════════════════════════
section .bss
alignb 4
vtick:      resd 1
vframe:     resd 1
vpx:        resd 1
vlives:     resd 1
vscore:     resd 1
vadir:      resd 1
vabx:       resd 1
vaby:       resd 1
vdead:      resd 1
vdone:      resd 1
vpbct:      resd 1
vabct:      resd 1
valive:     resb NALI
vpbx:       resd MAXPB
vpby:       resd MAXPB
vabx_d:     resd MAXAB
vaby_d:     resd MAXAB
vbx:        resd NBAR
vbhp:       resd NBAR
msg_over:   db "GAME OVER", 0
