; =============================================================================
; zork — a small text adventure (Great Underground Empire homage)
; =============================================================================
; A faithful-in-spirit clone of the opening of Zork I.
;
; Goal: open the mailbox, find your way into the white house, move the rug,
;       open the trap door, light the brass lantern, descend into the dark
;       cellar (beware the grue!), grab the treasure, and place it in the
;       trophy case in the living room.
;
; Commands: look (l), n/s/e/w/u/d, go <dir>, take <thing>, open <thing>,
;           read leaflet, move rug, turn on/off lamp, put treasure in case,
;           inventory (i), help, quit (q)
;
; ABI contract: see docs/abi-contract.md — ebp anchors the runtime base,
; every label reference goes through ebp, all buffers are in-file zero blocks.
; =============================================================================
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

    mov eax, 4                  ; sys_cls
    int 0x80

    lea esi, [ebp + banner]
    mov eax, 2                  ; sys_print_cr
    int 0x80
    mov eax, 3                  ; sys_newline
    int 0x80

    call print_room

.mainloop:
    lea esi, [ebp + prompt]
    mov eax, 1                  ; sys_print (no conversion)
    int 0x80

    call read_line
    lea esi, [ebp + inbuf]
    call to_lower

    call handle

    cmp byte [ebp + done], 0
    je .mainloop
    ret

; ──────────────────────────────────────────────────────────────────────────
; read_line — busy-poll the keyboard into inbuf, echoing as we go.
;   Enter (13) ends the line, Backspace (8) erases. Control chars ignored.
; ──────────────────────────────────────────────────────────────────────────
read_line:
    lea edi, [ebp + inbuf]
    xor ecx, ecx
.rl:
    mov eax, 7                  ; sys_get_key
    int 0x80
    test al, al
    jz .rl
    cmp al, 13
    je .rl_done
    cmp al, 8
    je .rl_bs
    cmp al, 32
    jb .rl                      ; ignore other control chars
    cmp ecx, 62
    jae .rl                     ; buffer full
    mov [edi + ecx], al
    inc ecx
    movzx ebx, al
    mov eax, 0                  ; sys_putchar
    int 0x80
    jmp .rl
.rl_bs:
    test ecx, ecx
    jz .rl
    dec ecx
    mov ebx, 8
    mov eax, 0
    int 0x80
    mov ebx, 32
    mov eax, 0
    int 0x80
    mov ebx, 8
    mov eax, 0
    int 0x80
    jmp .rl
.rl_done:
    mov byte [edi + ecx], 0
    mov eax, 3                  ; sys_newline
    int 0x80
    ret

; ──────────────────────────────────────────────────────────────────────────
; to_lower — lowercase the NUL-terminated string at esi (in place).
; ──────────────────────────────────────────────────────────────────────────
to_lower:
    push esi
.tl:
    mov al, [esi]
    test al, al
    jz .tl_done
    cmp al, 'A'
    jb .tl_next
    cmp al, 'Z'
    ja .tl_next
    add al, 0x20
    mov [esi], al
.tl_next:
    inc esi
    jmp .tl
.tl_done:
    pop esi
    ret

; ──────────────────────────────────────────────────────────────────────────
; find — substring search. esi = haystack, edi = needle.
;   Returns eax = 1 if needle occurs in haystack, else 0. Preserves regs.
; ──────────────────────────────────────────────────────────────────────────
find:
    push ebx
    push ecx
    push esi
    push edi
.f_try:
    mov al, [esi]
    test al, al
    jz .f_fail
    mov ebx, esi
    mov ecx, edi
.f_cmp:
    mov al, [ecx]
    test al, al
    jz .f_ok
    mov ah, [ebx]
    cmp ah, al
    jne .f_adv
    inc ebx
    inc ecx
    jmp .f_cmp
.f_adv:
    inc esi
    jmp .f_try
.f_ok:
    mov eax, 1
    jmp .f_ret
.f_fail:
    xor eax, eax
.f_ret:
    pop edi
    pop esi
    pop ecx
    pop ebx
    ret

; ──────────────────────────────────────────────────────────────────────────
; chk1 — is inbuf exactly the single character cl followed by NUL?
;   esi must point at inbuf. Returns eax = 1 / 0.
; ──────────────────────────────────────────────────────────────────────────
chk1:
    mov al, [esi]
    cmp al, cl
    jne .c_no
    cmp byte [esi + 1], 0
    jne .c_no
    mov eax, 1
    ret
.c_no:
    xor eax, eax
    ret

; ──────────────────────────────────────────────────────────────────────────
; print_room — print the current room description plus any items present.
; ──────────────────────────────────────────────────────────────────────────
print_room:
    pushad
    movzx eax, byte [ebp + room]
    lea ebx, [ebp + room_descs]
    mov esi, [ebx + eax * 4]
    add esi, ebp
    mov eax, 2
    int 0x80

    movzx eax, byte [ebp + room]
    cmp eax, 0
    je .pr0
    cmp eax, 5
    je .pr5
    cmp eax, 6
    je .pr6
    jmp .pr_done

.pr0:
    cmp byte [ebp + f_mailbox], 0
    je .pr_done
    cmp byte [ebp + t_leaf], 0
    jne .pr_done
    lea esi, [ebp + s_mailleaf]
    mov eax, 2
    int 0x80
    jmp .pr_done

.pr5:
    cmp byte [ebp + t_lamp], 0
    jne .pr5b
    lea esi, [ebp + s_lamphere]
    mov eax, 2
    int 0x80
.pr5b:
    cmp byte [ebp + t_sword], 0
    jne .pr5c
    lea esi, [ebp + s_swordhere]
    mov eax, 2
    int 0x80
.pr5c:
    cmp byte [ebp + f_rug], 0
    jne .pr5d
    lea esi, [ebp + s_rughere]
    mov eax, 2
    int 0x80
    jmp .pr_done
.pr5d:
    cmp byte [ebp + f_trap], 0
    jne .pr5open
    lea esi, [ebp + s_trapclosed]
    mov eax, 2
    int 0x80
    jmp .pr_done
.pr5open:
    lea esi, [ebp + s_trapopen]
    mov eax, 2
    int 0x80
    jmp .pr_done

.pr6:
    cmp byte [ebp + t_treas], 0
    jne .pr_done
    lea esi, [ebp + s_treashere]
    mov eax, 2
    int 0x80

.pr_done:
    popad
    ret

; ──────────────────────────────────────────────────────────────────────────
; do_move — move in direction ecx (0=N 1=S 2=E 3=W 4=U 5=D).
; ──────────────────────────────────────────────────────────────────────────
do_move:
    movzx eax, byte [ebp + room]

    ; living room + down = trap door / cellar
    cmp eax, 5
    jne .nm5
    cmp ecx, 5
    jne .nm5
    cmp byte [ebp + f_trap], 0
    je .blocked_here
    cmp byte [ebp + f_lampon], 0
    jne .entercellar
    ; dark cellar without light: a grue gets you
    lea esi, [ebp + m_grue]
    mov eax, 2
    int 0x80
    mov byte [ebp + done], 1
    ret
.entercellar:
    mov byte [ebp + room], 6
    call print_room
    ret
.blocked_here:
    lea esi, [ebp + m_cantgo]
    jmp .pm

.nm5:
    ; behind house + west = window into kitchen
    cmp eax, 3
    jne .normal
    cmp ecx, 3
    jne .normal
    cmp byte [ebp + window_open], 0
    jne .gokitchen
    lea esi, [ebp + m_winclosed]
    jmp .pm
.gokitchen:
    mov byte [ebp + room], 4
    call print_room
    ret

.normal:
    imul eax, 6
    add eax, ecx
    lea ebx, [ebp + exit_table]
    movzx edx, byte [ebx + eax]
    cmp dl, 0xFF
    jne .doexit
    ; blocked — special message for the boarded front door
    movzx eax, byte [ebp + room]
    cmp eax, 0
    jne .blk_generic
    cmp ecx, 2
    jne .blk_generic
    lea esi, [ebp + m_boarded]
    jmp .pm
.blk_generic:
    lea esi, [ebp + m_cantgo]
    jmp .pm
.doexit:
    mov [ebp + room], dl
    call print_room
    ret
.pm:
    mov eax, 2
    int 0x80
    ret

; ──────────────────────────────────────────────────────────────────────────
; handle — parse and execute one command line from inbuf.
; ──────────────────────────────────────────────────────────────────────────
handle:
    ; --- single-letter shortcuts ---
    lea esi, [ebp + inbuf]
    mov cl, 'q'
    call chk1
    test eax, eax
    jnz .quit
    lea esi, [ebp + inbuf]
    mov cl, 'l'
    call chk1
    test eax, eax
    jnz .look
    lea esi, [ebp + inbuf]
    mov cl, 'i'
    call chk1
    test eax, eax
    jnz .inv
    lea esi, [ebp + inbuf]
    mov cl, 'n'
    call chk1
    test eax, eax
    jnz .d0
    lea esi, [ebp + inbuf]
    mov cl, 's'
    call chk1
    test eax, eax
    jnz .d1
    lea esi, [ebp + inbuf]
    mov cl, 'e'
    call chk1
    test eax, eax
    jnz .d2
    lea esi, [ebp + inbuf]
    mov cl, 'w'
    call chk1
    test eax, eax
    jnz .d3
    lea esi, [ebp + inbuf]
    mov cl, 'u'
    call chk1
    test eax, eax
    jnz .d4
    lea esi, [ebp + inbuf]
    mov cl, 'd'
    call chk1
    test eax, eax
    jnz .d5

    ; --- word verbs (substring match anywhere in the line) ---
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nq]
    call find
    test eax, eax
    jnz .quit
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nhelp]
    call find
    test eax, eax
    jnz .help
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nlook]
    call find
    test eax, eax
    jnz .look
    lea esi, [ebp + inbuf]
    lea edi, [ebp + ninv]
    call find
    test eax, eax
    jnz .inv

    lea esi, [ebp + inbuf]
    lea edi, [ebp + ntake]
    call find
    test eax, eax
    jnz .take
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nget]
    call find
    test eax, eax
    jnz .take
    lea esi, [ebp + inbuf]
    lea edi, [ebp + ngrab]
    call find
    test eax, eax
    jnz .take
    lea esi, [ebp + inbuf]
    lea edi, [ebp + npick]
    call find
    test eax, eax
    jnz .take

    lea esi, [ebp + inbuf]
    lea edi, [ebp + ndrop]
    call find
    test eax, eax
    jnz .drop
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nopen]
    call find
    test eax, eax
    jnz .open
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nread]
    call find
    test eax, eax
    jnz .read
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nmove]
    call find
    test eax, eax
    jnz .moverug
    lea esi, [ebp + inbuf]
    lea edi, [ebp + npush]
    call find
    test eax, eax
    jnz .moverug

    lea esi, [ebp + inbuf]
    lea edi, [ebp + nturnoff]
    call find
    test eax, eax
    jnz .lampoff
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nturnon]
    call find
    test eax, eax
    jnz .lampon
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nlight]
    call find
    test eax, eax
    jnz .lampon

    lea esi, [ebp + inbuf]
    lea edi, [ebp + nput]
    call find
    test eax, eax
    jnz .put

    ; --- direction words ---
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nnorth]
    call find
    test eax, eax
    jnz .d0
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nsouth]
    call find
    test eax, eax
    jnz .d1
    lea esi, [ebp + inbuf]
    lea edi, [ebp + neast]
    call find
    test eax, eax
    jnz .d2
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nwest]
    call find
    test eax, eax
    jnz .d3
    lea esi, [ebp + inbuf]
    lea edi, [ebp + nup]
    call find
    test eax, eax
    jnz .d4
    lea esi, [ebp + inbuf]
    lea edi, [ebp + ndown]
    call find
    test eax, eax
    jnz .d5

    ; nothing matched
    lea esi, [ebp + m_huh]
    mov eax, 2
    int 0x80
    ret

.d0:
    mov ecx, 0
    jmp do_move
.d1:
    mov ecx, 1
    jmp do_move
.d2:
    mov ecx, 2
    jmp do_move
.d3:
    mov ecx, 3
    jmp do_move
.d4:
    mov ecx, 4
    jmp do_move
.d5:
    mov ecx, 5
    jmp do_move

.look:
    call print_room
    ret

.quit:
    mov byte [ebp + done], 1
    lea esi, [ebp + m_bye]
    mov eax, 2
    int 0x80
    ret

.help:
    lea esi, [ebp + help_txt]
    mov eax, 2
    int 0x80
    ret

.drop:
    lea esi, [ebp + m_keep]
    mov eax, 2
    int 0x80
    ret

.inv:
    mov al, [ebp + t_lamp]
    or al, [ebp + t_sword]
    or al, [ebp + t_leaf]
    or al, [ebp + t_treas]
    test al, al
    jnz .inv_some
    lea esi, [ebp + m_emptyhand]
    mov eax, 2
    int 0x80
    ret
.inv_some:
    lea esi, [ebp + m_carrying]
    mov eax, 2
    int 0x80
    cmp byte [ebp + t_lamp], 0
    je .iv1
    lea esi, [ebp + i_lamp]
    mov eax, 2
    int 0x80
.iv1:
    cmp byte [ebp + t_sword], 0
    je .iv2
    lea esi, [ebp + i_sword]
    mov eax, 2
    int 0x80
.iv2:
    cmp byte [ebp + t_leaf], 0
    je .iv3
    lea esi, [ebp + i_leaf]
    mov eax, 2
    int 0x80
.iv3:
    cmp byte [ebp + t_treas], 0
    je .iv4
    lea esi, [ebp + i_treas]
    mov eax, 2
    int 0x80
.iv4:
    ret

.take:
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_lamp]
    call find
    test eax, eax
    jnz .tk_lamp
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_lantern]
    call find
    test eax, eax
    jnz .tk_lamp
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_sword]
    call find
    test eax, eax
    jnz .tk_sword
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_leaflet]
    call find
    test eax, eax
    jnz .tk_leaf
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_treasure]
    call find
    test eax, eax
    jnz .tk_treas
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_jewel]
    call find
    test eax, eax
    jnz .tk_treas
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_gold]
    call find
    test eax, eax
    jnz .tk_treas
    lea esi, [ebp + m_canttake]
    mov eax, 2
    int 0x80
    ret
.tk_lamp:
    cmp byte [ebp + room], 5
    jne .tk_no
    cmp byte [ebp + t_lamp], 0
    jne .tk_no
    mov byte [ebp + t_lamp], 1
    jmp .tk_ok
.tk_sword:
    cmp byte [ebp + room], 5
    jne .tk_no
    cmp byte [ebp + t_sword], 0
    jne .tk_no
    mov byte [ebp + t_sword], 1
    jmp .tk_ok
.tk_leaf:
    cmp byte [ebp + room], 0
    jne .tk_no
    cmp byte [ebp + f_mailbox], 0
    je .tk_no
    cmp byte [ebp + t_leaf], 0
    jne .tk_no
    mov byte [ebp + t_leaf], 1
    jmp .tk_ok
.tk_treas:
    cmp byte [ebp + room], 6
    jne .tk_no
    cmp byte [ebp + t_treas], 0
    jne .tk_no
    mov byte [ebp + t_treas], 1
    jmp .tk_ok
.tk_ok:
    lea esi, [ebp + m_taken]
    mov eax, 2
    int 0x80
    ret
.tk_no:
    lea esi, [ebp + m_cantsee]
    mov eax, 2
    int 0x80
    ret

.open:
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_mailbox]
    call find
    test eax, eax
    jnz .op_mail
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_window]
    call find
    test eax, eax
    jnz .op_win
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_trap]
    call find
    test eax, eax
    jnz .op_trap
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_door]
    call find
    test eax, eax
    jnz .op_trap
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_case]
    call find
    test eax, eax
    jnz .op_case
    lea esi, [ebp + m_cantopen]
    mov eax, 2
    int 0x80
    ret
.op_mail:
    cmp byte [ebp + room], 0
    jne .op_nomail
    mov byte [ebp + f_mailbox], 1
    cmp byte [ebp + t_leaf], 0
    jne .op_mailempty
    lea esi, [ebp + m_mailopen_r]
    mov eax, 2
    int 0x80
    ret
.op_mailempty:
    lea esi, [ebp + m_mailopen_e]
    mov eax, 2
    int 0x80
    ret
.op_nomail:
    lea esi, [ebp + m_nomail]
    mov eax, 2
    int 0x80
    ret
.op_win:
    cmp byte [ebp + room], 3
    jne .op_nowin
    mov byte [ebp + window_open], 1
    lea esi, [ebp + m_winopen]
    mov eax, 2
    int 0x80
    ret
.op_nowin:
    lea esi, [ebp + m_nowin]
    mov eax, 2
    int 0x80
    ret
.op_trap:
    cmp byte [ebp + room], 5
    jne .op_notrap
    cmp byte [ebp + f_rug], 0
    je .op_notrap
    mov byte [ebp + f_trap], 1
    lea esi, [ebp + m_trapopened]
    mov eax, 2
    int 0x80
    ret
.op_notrap:
    lea esi, [ebp + m_notrap]
    mov eax, 2
    int 0x80
    ret
.op_case:
    cmp byte [ebp + room], 5
    jne .op_notrap
    lea esi, [ebp + m_caseopen]
    mov eax, 2
    int 0x80
    ret

.read:
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_leaflet]
    call find
    test eax, eax
    jz .rd_huh
    cmp byte [ebp + t_leaf], 0
    jne .rd_yes
    cmp byte [ebp + room], 0
    jne .rd_nohave
    cmp byte [ebp + f_mailbox], 0
    je .rd_nohave
.rd_yes:
    lea esi, [ebp + leaflet_txt]
    mov eax, 2
    int 0x80
    ret
.rd_huh:
    lea esi, [ebp + m_huh]
    mov eax, 2
    int 0x80
    ret
.rd_nohave:
    lea esi, [ebp + m_nohave]
    mov eax, 2
    int 0x80
    ret

.moverug:
    lea esi, [ebp + inbuf]
    lea edi, [ebp + n_rug]
    call find
    test eax, eax
    jz .mr_no
    cmp byte [ebp + room], 5
    jne .mr_no
    mov byte [ebp + f_rug], 1
    lea esi, [ebp + m_rugmoved]
    mov eax, 2
    int 0x80
    ret
.mr_no:
    lea esi, [ebp + m_norug]
    mov eax, 2
    int 0x80
    ret

.lampon:
    cmp byte [ebp + t_lamp], 0
    je .lamp_no
    mov byte [ebp + f_lampon], 1
    lea esi, [ebp + m_lampon]
    mov eax, 2
    int 0x80
    ret
.lampoff:
    cmp byte [ebp + t_lamp], 0
    je .lamp_no
    mov byte [ebp + f_lampon], 0
    lea esi, [ebp + m_lampoff]
    mov eax, 2
    int 0x80
    ret
.lamp_no:
    lea esi, [ebp + m_nolamp]
    mov eax, 2
    int 0x80
    ret

.put:
    cmp byte [ebp + room], 5
    jne .pt_here
    cmp byte [ebp + t_treas], 0
    je .pt_nothing
    mov byte [ebp + t_treas], 0
    mov byte [ebp + treas_done], 1
    mov byte [ebp + done], 1
    lea esi, [ebp + m_win]
    mov eax, 2
    int 0x80
    ret
.pt_nothing:
    lea esi, [ebp + m_putnothing]
    mov eax, 2
    int 0x80
    ret
.pt_here:
    lea esi, [ebp + m_puthere]
    mov eax, 2
    int 0x80
    ret

; ══════════════════════════════════════════════════════════════════════════
; READ-ONLY DATA
; ══════════════════════════════════════════════════════════════════════════
banner:       db "ZORK: The Great Underground Empire", 13
              db "An adventure clone for this OS. Type 'help' for commands.", 13, 0
prompt:       db "> ", 0

room_descs:   dd desc0, desc1, desc2, desc3, desc4, desc5, desc6, desc7

desc0:        db "West of House", 13
              db "You are standing in an open field west of a white house,", 13
              db "with a boarded front door. A small mailbox is here.", 13, 0
desc1:        db "North of House", 13
              db "You are facing the north side of the white house.", 13, 0
desc2:        db "South of House", 13
              db "You are facing the south side of the white house.", 13, 0
desc3:        db "Behind House", 13
              db "You are behind the white house. A small window is here.", 13, 0
desc4:        db "Kitchen", 13
              db "You are in the kitchen. A passage leads west; a window", 13
              db "opens to the east.", 13, 0
desc5:        db "Living Room", 13
              db "You are in the living room. A trophy case stands here.", 13, 0
desc6:        db "Cellar", 13
              db "You are in a dark, damp cellar with a narrow passage.", 13, 0
desc7:        db "Forest", 13
              db "This is a dim forest, with trees in all directions.", 13, 0

s_mailleaf:   db "The mailbox contains a leaflet.", 13, 0
s_lamphere:   db "A battery-powered brass lantern is here.", 13, 0
s_swordhere:  db "An elvish sword of great antiquity lies here.", 13, 0
s_rughere:    db "A large oriental rug covers the center of the floor.", 13, 0
s_trapclosed: db "Beneath the rug is a closed trap door.", 13, 0
s_trapopen:   db "An open trap door descends into darkness.", 13, 0
s_treashere:  db "A pile of jewels and gold rests in the corner.", 13, 0

m_taken:      db "Taken.", 13, 0
m_cantsee:    db "You don't see that here.", 13, 0
m_canttake:   db "You can't take that.", 13, 0
m_boarded:    db "The door is boarded and you can't remove the boards.", 13, 0
m_cantgo:     db "You can't go that way.", 13, 0
m_winclosed:  db "The window is closed.", 13, 0
m_winopen:    db "With great effort you open the window far enough to enter.", 13, 0
m_nowin:      db "There is no window here.", 13, 0
m_nomail:     db "You see no mailbox here.", 13, 0
m_mailopen_r: db "Opening the mailbox reveals a leaflet.", 13, 0
m_mailopen_e: db "The mailbox is empty.", 13, 0
m_rugmoved:   db "You move the rug aside, revealing a closed trap door.", 13, 0
m_norug:      db "There is no rug here.", 13, 0
m_trapopened: db "The trap door creaks open, revealing a dark cellar.", 13, 0
m_notrap:     db "You see no trap door here.", 13, 0
m_caseopen:   db "The trophy case is open.", 13, 0
m_grue:       db "It is pitch black. You are likely to be eaten by a grue.", 13
              db "*** You have died ***", 13, 0
m_lampon:     db "The brass lantern is now on.", 13, 0
m_lampoff:    db "The brass lantern is now off.", 13, 0
m_nolamp:     db "You don't have a lamp.", 13, 0
m_cantopen:   db "You can't open that.", 13, 0
m_nohave:     db "You aren't carrying that.", 13, 0
m_keep:       db "You'd better hold on to that.", 13, 0
m_win:        db "You place the treasure in the case and it gleams brightly.", 13
              db "*** You have won! ***", 13, 0
m_putnothing: db "You have nothing to put in the case.", 13, 0
m_puthere:    db "You can't put anything there.", 13, 0
m_huh:        db "I don't understand that.", 13, 0
m_emptyhand:  db "You are empty-handed.", 13, 0
m_carrying:   db "You are carrying:", 13, 0
m_bye:        db "Thanks for playing!", 13, 0
i_lamp:       db "  A brass lantern", 13, 0
i_sword:      db "  An elvish sword", 13, 0
i_leaf:       db "  A leaflet", 13, 0
i_treas:      db "  A pile of treasure", 13, 0

leaflet_txt:  db "WELCOME TO ZORK!", 13
              db "Your goal is to find the treasure below the white house", 13
              db "and place it in the trophy case in the living room.", 13
              db "Beware the grue lurking in the dark!", 13, 0
help_txt:     db "Commands: look, n/s/e/w/u/d, take <thing>, open <thing>,", 13
              db "read leaflet, move rug, turn on lamp, turn off lamp,", 13
              db "put treasure in case, inventory, quit.", 13, 0

; exit table: 8 rooms x 6 dirs (N,S,E,W,U,D); 0xFF = no plain exit
exit_table:
              db 1,    2,    0xFF, 7,    0xFF, 0xFF  ; 0 West of House
              db 0xFF, 0xFF, 3,    0,    0xFF, 0xFF  ; 1 North of House
              db 0xFF, 0xFF, 3,    0,    0xFF, 0xFF  ; 2 South of House
              db 1,    2,    7,    0xFF, 0xFF, 0xFF  ; 3 Behind House (W=window)
              db 0xFF, 0xFF, 3,    5,    0xFF, 0xFF  ; 4 Kitchen
              db 0xFF, 0xFF, 4,    0xFF, 0xFF, 0xFF  ; 5 Living Room (D=trap)
              db 0xFF, 0xFF, 0xFF, 0xFF, 5,    0xFF  ; 6 Cellar
              db 0xFF, 0xFF, 0xFF, 0,    0xFF, 0xFF  ; 7 Forest

; needle words for the parser
nq:           db "quit", 0
nhelp:        db "help", 0
nlook:        db "look", 0
ninv:         db "invent", 0
ntake:        db "take", 0
nget:         db "get", 0
ngrab:        db "grab", 0
npick:        db "pick", 0
ndrop:        db "drop", 0
nopen:        db "open", 0
nread:        db "read", 0
nmove:        db "move", 0
npush:        db "push", 0
nturnon:      db "turn on", 0
nturnoff:     db "turn off", 0
nlight:       db "light", 0
nput:         db "put", 0
nnorth:       db "north", 0
nsouth:       db "south", 0
neast:        db "east", 0
nwest:        db "west", 0
nup:          db "up", 0
ndown:        db "down", 0
n_mailbox:    db "mailbox", 0
n_window:     db "window", 0
n_trap:       db "trap", 0
n_door:       db "door", 0
n_rug:        db "rug", 0
n_lamp:       db "lamp", 0
n_lantern:    db "lantern", 0
n_sword:      db "sword", 0
n_leaflet:    db "leaflet", 0
n_treasure:   db "treasure", 0
n_jewel:      db "jewel", 0
n_gold:       db "gold", 0
n_case:       db "case", 0

; ══════════════════════════════════════════════════════════════════════════
; MUTABLE STATE — in-file zero buffers (ABI rule 3: no section .bss)
; ══════════════════════════════════════════════════════════════════════════
align 4
inbuf:        times 64 db 0
room:         db 0          ; current room (0 = West of House)
f_mailbox:    db 0          ; mailbox opened
window_open:  db 0          ; window opened
f_rug:        db 0          ; rug moved aside
f_trap:       db 0          ; trap door opened
f_lampon:     db 0          ; lantern lit
t_lamp:       db 0          ; carrying lantern
t_sword:      db 0          ; carrying sword
t_leaf:       db 0          ; carrying leaflet
t_treas:      db 0          ; carrying treasure
treas_done:   db 0          ; treasure placed in case
done:         db 0          ; quit / game over flag
