; =============================================================================
; elite — wireframe space sim for the x86 Assembly Kernel (homage to Elite 1984)
; =============================================================================
; First iteration: VGA Mode 13h (320x200x256), split-screen flight view +
; dashboard. A rotating wireframe Cobra Mk III, streaming stardust, a scanner
; box and fuel/energy bars. Fixed-point 3D math, no FPU.
;
; Controls:  W/S = pitch   A/D = roll   Q or ESC = quit
;
; Builds on the kernel graphics engine (syscalls 36-47, double-buffered).
; =============================================================================

[bits 32]
[org 0x00000000]

%include "userland.inc"

; ── Syscalls ────────────────────────────────────────────────────────────────
%define SYS_GETKEY        7
%define SYS_TICK          8
%define SYS_GFX_ENTER     36
%define SYS_GFX_EXIT      37
%define SYS_GFX_CLEAR     38
%define SYS_GFX_PIXEL     39
%define SYS_GFX_FILLRECT  40
%define SYS_GFX_RECT      41
%define SYS_GFX_LINE      42
%define SYS_GFX_CHAR      43
%define SYS_GFX_STRING    44
%define SYS_GFX_BLIT      45
%define SYS_GFX_INFO      46

; ── 3-3-2 palette colours (index = RRRGGGBB) ────────────────────────────────
%define COL_BLACK    0x00
%define COL_GREEN    0x1C
%define COL_CYAN     0x1F
%define COL_RED      0xE0
%define COL_YELLOW   0xFC
%define COL_WHITE    0xFF

%define COL_SHIP     COL_WHITE
%define COL_STAR     COL_WHITE
%define COL_SCANNER  COL_RED
%define COL_FUEL     COL_GREEN
%define COL_ENGY     COL_RED
%define COL_TEXT     COL_GREEN
%define COL_SEP      COL_WHITE
%define COL_TITLE    COL_CYAN

; ── Scene constants ─────────────────────────────────────────────────────────
%define NVERTS    6
%define NEDGES    8
%define NSTARS    64
%define NSHIPS    3
%define CX        160       ; projection centre x
%define CY        84        ; projection centre y (above dashboard)
%define FOCAL     240       ; ship focal length
%define ZDIST     300       ; ship distance from camera
%define FOCAL_S   110       ; stardust focal length
%define STAR_SPD  7         ; stardust approach speed per frame
%define NEARZ     120       ; nearest visible z before a ship flies past
%define RANGE     1700      ; max laser kill range
%define HITR      16        ; crosshair hit radius (px)
%define EXPFR     12        ; explosion frames

; =============================================================================
_start:
    call .anchor
.anchor:
    pop ebp
    sub ebp, .anchor

    mov dword [ebp + rngstate], 0x00C0FFEE
    mov dword [ebp + speed], 14
    mov dword [ebp + energy], 96
    mov dword [ebp + kills], 0
    mov byte  [ebp + gmode], 0
    ; Galaxy 0 starting seeds (classic Elite)
    mov dword [ebp + seed0], 0x5A4A
    mov dword [ebp + seed1], 0x0248
    mov dword [ebp + seed2], 0xB753
    call gen_sin
    call stars_init
    call ships_init
    call compute_system

    mov eax, SYS_GFX_ENTER
    int 0x80

; ── Main loop ────────────────────────────────────────────────────────────────
.loop:
    ; throttle to the 100 Hz tick
    mov eax, SYS_TICK
    int 0x80
    mov [ebp + last_tick], eax
.wait:
    mov eax, SYS_TICK
    int 0x80
    cmp eax, [ebp + last_tick]
    je .wait

    cmp byte [ebp + gmode], 0
    jne .datamode

    call do_input
    cmp byte [ebp + quitf], 0
    jne .exit
    cmp byte [ebp + gmode], 0          ; 'i' may have switched us to data
    jne .loop

    call update_angles
    call update_ships

    mov eax, SYS_GFX_CLEAR
    mov ebx, COL_BLACK
    int 0x80

    call stars_step
    call draw_station
    call draw_ships
    call do_combat
    call draw_laser
    call draw_dash

    mov eax, SYS_GFX_BLIT
    int 0x80
    jmp .loop

.datamode:
    call data_input
    cmp byte [ebp + quitf], 0
    jne .exit
    call draw_data_screen
    mov eax, SYS_GFX_BLIT
    int 0x80
    jmp .loop

.exit:
    mov eax, SYS_GFX_EXIT
    int 0x80
    ret

; =============================================================================
; Input — drain the key buffer, update rotation, auto-spin when idle
; =============================================================================
do_input:
    mov byte [ebp + firereq], 0
.k:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz .done
    cmp al, 0x1B
    je .quit
    cmp al, 'q'
    je .quit
    cmp al, 'w'
    je .pup
    cmp al, 's'
    je .pdn
    cmp al, 'a'
    je .rl
    cmp al, 'd'
    je .rr
    cmp al, ' '
    je .fire
    cmp al, 'i'
    je .info
    jmp .k
.pup:
    sub dword [ebp + angX], 4
    jmp .k
.pdn:
    add dword [ebp + angX], 4
    jmp .k
.rl:
    sub dword [ebp + angY], 4
    jmp .k
.rr:
    add dword [ebp + angY], 4
    jmp .k
.fire:
    mov byte [ebp + firereq], 1
    jmp .k
.info:
    mov byte [ebp + gmode], 1
    jmp .k
.done:
    ret
.quit:
    mov byte [ebp + quitf], 1
    ret

; =============================================================================
; data_input — System Data screen keys: N next system, F flight, Q/ESC quit
; =============================================================================
data_input:
.k:
    mov eax, SYS_GETKEY
    int 0x80
    test al, al
    jz .done
    cmp al, 0x1B
    je .quit
    cmp al, 'q'
    je .quit
    cmp al, 'f'
    je .flight
    cmp al, 'n'
    je .next
    jmp .k
.flight:
    mov byte [ebp + gmode], 0
    jmp .k
.next:
    call hyperspace
    jmp .k
.done:
    ret
.quit:
    mov byte [ebp + quitf], 1
    ret

; =============================================================================
; update_angles — resolve sin/cos for the current pitch (X) and roll (Y)
; =============================================================================
update_angles:
    mov eax, [ebp + angX]
    and eax, 0xFF
    movsx ebx, word [ebp + sine_tab + eax*2]
    mov [ebp + sinX], ebx
    mov eax, [ebp + angX]
    add eax, 64
    and eax, 0xFF
    movsx ebx, word [ebp + sine_tab + eax*2]
    mov [ebp + cosX], ebx

    mov eax, [ebp + angY]
    and eax, 0xFF
    movsx ebx, word [ebp + sine_tab + eax*2]
    mov [ebp + sinY], ebx
    mov eax, [ebp + angY]
    add eax, 64
    and eax, 0xFF
    movsx ebx, word [ebp + sine_tab + eax*2]
    mov [ebp + cosY], ebx
    ret

; =============================================================================
; rotate_view — rotate (tx,ty,tz) by player pitch (angX) and yaw (angY)
;   out: rvx, rvy, rvz
; =============================================================================
rotate_view:
    ; pitch about X: y1 = (ty*cosX - tz*sinX)>>8
    mov eax, [ebp + ty]
    imul eax, [ebp + cosX]
    mov ecx, [ebp + tz]
    imul ecx, [ebp + sinX]
    sub eax, ecx
    sar eax, 8
    mov [ebp + rvy], eax
    ; z1 = (ty*sinX + tz*cosX)>>8
    mov eax, [ebp + ty]
    imul eax, [ebp + sinX]
    mov ecx, [ebp + tz]
    imul ecx, [ebp + cosX]
    add eax, ecx
    sar eax, 8
    mov [ebp + rvz1], eax
    ; yaw about Y: x2 = (tx*cosY + z1*sinY)>>8
    mov eax, [ebp + tx]
    imul eax, [ebp + cosY]
    mov ecx, [ebp + rvz1]
    imul ecx, [ebp + sinY]
    add eax, ecx
    sar eax, 8
    mov [ebp + rvx], eax
    ; z2 = (z1*cosY - tx*sinY)>>8
    mov eax, [ebp + rvz1]
    imul eax, [ebp + cosY]
    mov ecx, [ebp + tx]
    imul ecx, [ebp + sinY]
    sub eax, ecx
    sar eax, 8
    mov [ebp + rvz], eax
    ret

; =============================================================================
; ships_init — scatter the enemy ships in world space
; =============================================================================
ships_init:
    mov dword [ebp + shp_i], 0
.l:
    mov ebx, [ebp + shp_i]
    call respawn_ship
    mov ebx, [ebp + shp_i]
    mov byte [ebp + shp_alive + ebx], 1
    mov byte [ebp + shp_exp + ebx], 0
    inc dword [ebp + shp_i]
    cmp dword [ebp + shp_i], NSHIPS
    jl .l
    ret

; respawn_ship — place ship index ebx far away with random offset
respawn_ship:
    push ebx
    call rand_wx
    pop ebx
    mov [ebp + shp_x + ebx*4], eax
    push ebx
    call rand_wy
    pop ebx
    mov [ebp + shp_y + ebx*4], eax
    push ebx
    call rand_wz
    pop ebx
    mov [ebp + shp_z + ebx*4], eax
    ret

; =============================================================================
; update_ships — advance motion, handle fly-past and explosion timers
; =============================================================================
update_ships:
    mov dword [ebp + shp_i], 0
.l:
    mov ebx, [ebp + shp_i]
    cmp byte [ebp + shp_alive + ebx], 0
    je .dead
    ; alive: approach the player
    mov eax, [ebp + shp_z + ebx*4]
    sub eax, [ebp + speed]
    mov [ebp + shp_z + ebx*4], eax
    cmp eax, NEARZ
    jg .next
    ; flew past — recycle
    call respawn_ship
    jmp .next
.dead:
    cmp byte [ebp + shp_exp + ebx], 0
    je .next
    dec byte [ebp + shp_exp + ebx]
    jnz .next
    ; explosion finished — respawn alive
    call respawn_ship
    mov ebx, [ebp + shp_i]
    mov byte [ebp + shp_alive + ebx], 1
.next:
    inc dword [ebp + shp_i]
    cmp dword [ebp + shp_i], NSHIPS
    jl .l
    ret

; =============================================================================
; draw_ships — project + draw each ship, store screen centre, draw scanner blip
; =============================================================================
draw_ships:
    mov dword [ebp + shp_i], 0
.l:
    mov ebx, [ebp + shp_i]
    cmp byte [ebp + shp_alive + ebx], 0
    je .exploding

    ; centre into camera space
    mov eax, [ebp + shp_x + ebx*4]
    mov [ebp + tx], eax
    mov eax, [ebp + shp_y + ebx*4]
    mov [ebp + ty], eax
    mov eax, [ebp + shp_z + ebx*4]
    mov [ebp + tz], eax
    call rotate_view
    mov eax, [ebp + rvx]
    mov [ebp + ccx], eax
    mov eax, [ebp + rvy]
    mov [ebp + ccy], eax
    mov eax, [ebp + rvz]
    mov [ebp + ccz], eax

    mov ebx, [ebp + shp_i]
    mov eax, [ebp + ccz]
    mov [ebp + shp_cz + ebx*4], eax
    cmp eax, NEARZ
    jl .offscr

    ; project centre
    mov eax, [ebp + ccx]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + ccz]
    add eax, CX
    mov ebx, [ebp + shp_i]
    mov [ebp + shp_sx + ebx*4], eax
    mov eax, [ebp + ccy]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + ccz]
    mov ecx, CY
    sub ecx, eax
    mov ebx, [ebp + shp_i]
    mov [ebp + shp_sy + ebx*4], ecx
    mov byte [ebp + shp_on + ebx], 1

    call project_ship_verts
    call draw_edges
    call draw_blip
    jmp .next
.offscr:
    mov ebx, [ebp + shp_i]
    mov byte [ebp + shp_on + ebx], 0
    jmp .next
.exploding:
    mov ebx, [ebp + shp_i]
    cmp byte [ebp + shp_exp + ebx], 0
    je .next
    call draw_explosion
.next:
    inc dword [ebp + shp_i]
    cmp dword [ebp + shp_i], NSHIPS
    jl .l
    ret

; project_ship_verts — rotate each local vertex, add ship centre, project
project_ship_verts:
    mov dword [ebp + vidx], 0
.vl:
    mov eax, [ebp + vidx]
    lea ebx, [eax + eax*2]
    movsx eax, byte [ebp + verts + ebx]
    mov [ebp + tx], eax
    movsx eax, byte [ebp + verts + ebx + 1]
    mov [ebp + ty], eax
    movsx eax, byte [ebp + verts + ebx + 2]
    mov [ebp + tz], eax
    call rotate_view
    ; camera = rotated local + ship centre
    mov eax, [ebp + rvx]
    add eax, [ebp + ccx]
    mov [ebp + camx], eax
    mov eax, [ebp + rvy]
    add eax, [ebp + ccy]
    mov [ebp + camy], eax
    mov eax, [ebp + rvz]
    add eax, [ebp + ccz]
    mov [ebp + camz], eax
    cmp eax, 8
    jl .invalid

    mov esi, [ebp + vidx]
    mov eax, [ebp + camx]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + camz]
    add eax, CX
    mov [ebp + proj_x + esi*4], eax
    mov eax, [ebp + camy]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + camz]
    mov ecx, CY
    sub ecx, eax
    mov [ebp + proj_y + esi*4], ecx
    mov byte [ebp + pvalid + esi], 1
    jmp .vnext
.invalid:
    mov esi, [ebp + vidx]
    mov byte [ebp + pvalid + esi], 0
.vnext:
    inc dword [ebp + vidx]
    cmp dword [ebp + vidx], NVERTS
    jl .vl
    ret

; draw_blip — radar dot for the current ship (uses ccx/ccy/ccz)
draw_blip:
    ; bx = 160 + ccx/32
    mov eax, [ebp + ccx]
    sar eax, 5
    add eax, 160
    cmp eax, 104
    jge .xok1
    mov eax, 104
.xok1:
    cmp eax, 216
    jle .xok2
    mov eax, 216
.xok2:
    mov [ebp + blip_x], eax
    ; by = 189 - ccz/128
    mov eax, [ebp + ccz]
    sar eax, 7
    mov ecx, 189
    sub ecx, eax
    cmp ecx, 163
    jge .yok1
    mov ecx, 163
.yok1:
    cmp ecx, 189
    jle .yok2
    mov ecx, 189
.yok2:
    mov [ebp + blip_y], ecx
    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + blip_x]
    mov ecx, [ebp + blip_y]
    mov edx, COL_YELLOW
    mov esi, 3
    mov edi, 3
    int 0x80
    ret

; draw_explosion — expanding ring at the ship's last screen centre
draw_explosion:
    mov ebx, [ebp + shp_i]
    movzx eax, byte [ebp + shp_exp + ebx]
    mov ecx, EXPFR
    sub ecx, eax                 ; frames elapsed
    lea ecx, [ecx*2 + 3]         ; radius
    mov ebx, [ebp + shp_i]
    mov eax, [ebp + shp_sx + ebx*4]
    mov [ebp + ce_x], eax
    mov eax, [ebp + shp_sy + ebx*4]
    mov [ebp + ce_y], eax
    mov [ebp + ce_rx], ecx
    mov [ebp + ce_ry], ecx
    mov dword [ebp + ce_col], COL_RED
    mov dword [ebp + ce_step], 32
    call draw_ellipse
    ret

; =============================================================================
; do_combat — fire the laser, destroy a ship centred on the crosshair
; =============================================================================
do_combat:
    cmp byte [ebp + firereq], 0
    je .ret
    mov dword [ebp + laser_timer], 3
    mov dword [ebp + shp_i], 0
.l:
    mov ebx, [ebp + shp_i]
    cmp byte [ebp + shp_on + ebx], 0
    je .next
    mov eax, [ebp + shp_cz + ebx*4]
    cmp eax, RANGE
    jg .next
    ; |sx - CX| <= HITR
    mov eax, [ebp + shp_sx + ebx*4]
    sub eax, CX
    cmp eax, 0
    jge .px
    neg eax
.px:
    cmp eax, HITR
    jg .next
    ; |sy - CY| <= HITR
    mov eax, [ebp + shp_sy + ebx*4]
    sub eax, CY
    cmp eax, 0
    jge .py
    neg eax
.py:
    cmp eax, HITR
    jg .next
    ; hit!
    mov byte [ebp + shp_alive + ebx], 0
    mov byte [ebp + shp_exp + ebx], EXPFR
    inc dword [ebp + kills]
    ret
.next:
    inc dword [ebp + shp_i]
    cmp dword [ebp + shp_i], NSHIPS
    jl .l
.ret:
    ret

; =============================================================================
; draw_laser — twin beams converging on the crosshair while firing
; =============================================================================
draw_laser:
    cmp dword [ebp + laser_timer], 0
    je .ret
    mov eax, SYS_GFX_LINE
    mov ebx, 0
    mov ecx, 148
    mov edx, CX
    mov esi, CY
    mov edi, COL_RED
    int 0x80
    mov eax, SYS_GFX_LINE
    mov ebx, 319
    mov ecx, 148
    mov edx, CX
    mov esi, CY
    mov edi, COL_RED
    int 0x80
    dec dword [ebp + laser_timer]
.ret:
    ret

; =============================================================================
; draw_station — distant wireframe ball as a world object
; =============================================================================
draw_station:
    mov dword [ebp + tx], 0
    mov dword [ebp + ty], 0
    mov dword [ebp + tz], 2600
    call rotate_view
    mov eax, [ebp + rvz]
    cmp eax, NEARZ
    jl .ret
    mov [ebp + st_z], eax
    ; project centre
    mov eax, [ebp + rvx]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + st_z]
    add eax, CX
    mov [ebp + ce_x], eax
    mov eax, [ebp + rvy]
    imul eax, FOCAL
    cdq
    idiv dword [ebp + st_z]
    mov ecx, CY
    sub ecx, eax
    mov [ebp + ce_y], ecx
    ; radius = 14000 / z
    mov eax, 14000
    cdq
    idiv dword [ebp + st_z]
    mov [ebp + ce_rx], eax
    mov [ebp + ce_ry], eax
    mov dword [ebp + ce_col], COL_WHITE
    mov dword [ebp + ce_step], 16
    call draw_ellipse
.ret:
    ret

; =============================================================================
; draw_edges — connect projected vertices
; =============================================================================
draw_edges:
    mov dword [ebp + ei], 0
.el:
    mov eax, [ebp + ei]
    shl eax, 1
    movzx ebx, byte [ebp + edges + eax]
    mov [ebp + ea], ebx
    movzx ecx, byte [ebp + edges + eax + 1]
    mov [ebp + eb], ecx

    mov ebx, [ebp + ea]
    cmp byte [ebp + pvalid + ebx], 0
    je .enext
    mov ecx, [ebp + eb]
    cmp byte [ebp + pvalid + ecx], 0
    je .enext

    mov ebx, [ebp + ea]
    mov ebx, [ebp + proj_x + ebx*4]      ; x0
    mov eax, [ebp + ea]
    mov ecx, [ebp + proj_y + eax*4]      ; y0
    mov eax, [ebp + eb]
    mov edx, [ebp + proj_x + eax*4]      ; x1
    mov eax, [ebp + eb]
    mov esi, [ebp + proj_y + eax*4]      ; y1
    mov edi, COL_SHIP
    mov eax, SYS_GFX_LINE
    int 0x80
.enext:
    inc dword [ebp + ei]
    cmp dword [ebp + ei], NEDGES
    jl .el
    ret

; =============================================================================
; Stardust
; =============================================================================
stars_init:
    mov dword [ebp + sidx], 0
.il:
    mov eax, [ebp + sidx]
    lea eax, [eax + eax*2]
    shl eax, 2                          ; offset = i*12
    mov [ebp + soff], eax
    call rand_x
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx], eax
    call rand_y
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx + 4], eax
    call rand_z
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx + 8], eax
    inc dword [ebp + sidx]
    cmp dword [ebp + sidx], NSTARS
    jl .il
    ret

stars_step:
    mov dword [ebp + sidx], 0
.sl:
    mov eax, [ebp + sidx]
    lea eax, [eax + eax*2]
    shl eax, 2
    mov [ebp + soff], eax
    mov ebx, eax
    mov ecx, [ebp + stars + ebx + 8]    ; z
    sub ecx, STAR_SPD
    cmp ecx, 12
    jg .keepz
    ; respawn far away
    call rand_x
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx], eax
    call rand_y
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx + 4], eax
    mov ecx, 255
.keepz:
    mov ebx, [ebp + soff]
    mov [ebp + stars + ebx + 8], ecx

    ; project
    mov eax, [ebp + stars + ebx]        ; x
    imul eax, FOCAL_S
    cdq
    idiv dword [ebp + stars + ebx + 8]
    add eax, CX
    mov [ebp + sxv], eax
    mov ebx, [ebp + soff]
    mov eax, [ebp + stars + ebx + 4]    ; y
    imul eax, FOCAL_S
    cdq
    idiv dword [ebp + stars + ebx + 8]
    mov ecx, CY
    sub ecx, eax
    mov [ebp + syv], ecx

    ; clip to the flight view
    mov eax, [ebp + sxv]
    cmp eax, 0
    jl .snext
    cmp eax, 319
    jg .snext
    mov ecx, [ebp + syv]
    cmp ecx, 12
    jl .snext
    cmp ecx, 148
    jg .snext

    mov ebx, eax
    mov edx, COL_STAR
    mov eax, SYS_GFX_PIXEL
    int 0x80
.snext:
    inc dword [ebp + sidx]
    cmp dword [ebp + sidx], NSTARS
    jl .sl
    ret

; =============================================================================
; Dashboard
; =============================================================================
draw_dash:
    ; split-screen separator
    mov eax, SYS_GFX_LINE
    mov ebx, 0
    mov ecx, 150
    mov edx, 319
    mov esi, 150
    mov edi, COL_SEP
    int 0x80

    ; ── elliptical scanner (centre) ──────────────────────────────────────────
    mov dword [ebp + ce_x], 160
    mov dword [ebp + ce_y], 176
    mov dword [ebp + ce_rx], 58
    mov dword [ebp + ce_ry], 15
    mov dword [ebp + ce_col], COL_SCANNER
    mov dword [ebp + ce_step], 16
    call draw_ellipse
    ; horizontal axis
    mov eax, SYS_GFX_LINE
    mov ebx, 102
    mov ecx, 176
    mov edx, 218
    mov esi, 176
    mov edi, COL_SCANNER
    int 0x80
    ; vertical axis
    mov eax, SYS_GFX_LINE
    mov ebx, 160
    mov ecx, 162
    mov edx, 160
    mov esi, 190
    mov edi, COL_SCANNER
    int 0x80

    ; ── left gauge column ────────────────────────────────────────────────────
    lea esi, [ebp + s_fs]
    mov dword [ebp + g_lx], 4
    mov dword [ebp + g_y], 156
    mov dword [ebp + g_bx], 24
    mov dword [ebp + g_bw], 36
    mov dword [ebp + g_col], COL_GREEN
    call draw_gauge
    lea esi, [ebp + s_as]
    mov dword [ebp + g_y], 166
    mov dword [ebp + g_bw], 32
    mov dword [ebp + g_col], COL_GREEN
    call draw_gauge
    lea esi, [ebp + s_fu]
    mov dword [ebp + g_y], 176
    mov dword [ebp + g_bw], 28
    mov dword [ebp + g_col], COL_YELLOW
    call draw_gauge
    lea esi, [ebp + s_ct]
    mov dword [ebp + g_y], 186
    mov dword [ebp + g_bw], 14
    mov dword [ebp + g_col], COL_GREEN
    call draw_gauge

    ; ── right gauge column ───────────────────────────────────────────────────
    lea esi, [ebp + s_lt]
    mov dword [ebp + g_lx], 248
    mov dword [ebp + g_y], 156
    mov dword [ebp + g_bx], 268
    mov dword [ebp + g_bw], 12
    mov dword [ebp + g_col], COL_RED
    call draw_gauge
    lea esi, [ebp + s_al]
    mov dword [ebp + g_y], 166
    mov dword [ebp + g_bw], 34
    mov dword [ebp + g_col], COL_GREEN
    call draw_gauge
    lea esi, [ebp + s_sp]
    mov dword [ebp + g_y], 176
    mov eax, [ebp + speed]
    add eax, eax                 ; speed bar = speed*2
    mov [ebp + g_bw], eax
    mov dword [ebp + g_col], COL_YELLOW
    call draw_gauge
    lea esi, [ebp + s_en]
    mov dword [ebp + g_y], 186
    mov eax, [ebp + energy]
    sar eax, 1                   ; energy bar = energy/2
    mov [ebp + g_bw], eax
    mov dword [ebp + g_col], COL_RED
    call draw_gauge

    ; ── ELITE caption (centre bottom) ────────────────────────────────────────
    lea esi, [ebp + s_elite]
    mov ebx, 140
    mov ecx, 192
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; ── Front View title ─────────────────────────────────────────────────────
    lea esi, [ebp + s_title]
    mov ebx, 120
    mov ecx, 4
    mov edx, COL_TITLE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; ── kill tally ──────────────────────────────────────────────────────────
    lea esi, [ebp + s_kills]
    mov ebx, 232
    mov ecx, 4
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    ; kill count digit (single char, 0-9 clamped)
    mov eax, [ebp + kills]
    cmp eax, 9
    jle .kdig
    mov eax, 9
.kdig:
    add eax, '0'
    mov ebx, eax                 ; char
    mov ecx, 280                 ; x
    mov edx, 4                   ; y
    mov esi, COL_WHITE           ; colour
    mov eax, SYS_GFX_CHAR
    int 0x80
    ret

; =============================================================================
; draw_ellipse — closed polyline approximation using the sine table
;   in: [ce_x],[ce_y] centre  [ce_rx],[ce_ry] radii  [ce_col] colour
;       [ce_step] angle increment (smaller = smoother)
; =============================================================================
draw_ellipse:
    xor eax, eax
    call ell_point
    mov ebx, [ebp + ce_nx]
    mov [ebp + ce_fx], ebx
    mov [ebp + ce_px], ebx
    mov ebx, [ebp + ce_ny]
    mov [ebp + ce_fy], ebx
    mov [ebp + ce_py], ebx
    mov eax, [ebp + ce_step]
    mov [ebp + ce_a], eax
.loop:
    cmp dword [ebp + ce_a], 256
    jge .close
    mov eax, [ebp + ce_a]
    and eax, 0xFF
    call ell_point
    mov ebx, [ebp + ce_px]
    mov ecx, [ebp + ce_py]
    mov edx, [ebp + ce_nx]
    mov esi, [ebp + ce_ny]
    mov edi, [ebp + ce_col]
    mov eax, SYS_GFX_LINE
    int 0x80
    mov ebx, [ebp + ce_nx]
    mov [ebp + ce_px], ebx
    mov ebx, [ebp + ce_ny]
    mov [ebp + ce_py], ebx
    mov eax, [ebp + ce_a]
    add eax, [ebp + ce_step]
    mov [ebp + ce_a], eax
    jmp .loop
.close:
    mov ebx, [ebp + ce_px]
    mov ecx, [ebp + ce_py]
    mov edx, [ebp + ce_fx]
    mov esi, [ebp + ce_fy]
    mov edi, [ebp + ce_col]
    mov eax, SYS_GFX_LINE
    int 0x80
    ret

; ell_point — in: eax = angle(0..255); out: [ce_nx],[ce_ny]
ell_point:
    push eax
    movsx ebx, word [ebp + sine_tab + eax*2]   ; sin
    imul ebx, [ebp + ce_rx]
    sar ebx, 8
    add ebx, [ebp + ce_x]
    mov [ebp + ce_nx], ebx
    pop eax
    add eax, 64
    and eax, 0xFF
    movsx ebx, word [ebp + sine_tab + eax*2]   ; cos
    imul ebx, [ebp + ce_ry]
    sar ebx, 8
    add ebx, [ebp + ce_y]
    mov [ebp + ce_ny], ebx
    ret

; =============================================================================
; draw_gauge — labelled indicator bar
;   in: esi = label  [g_lx] label x  [g_y] row y  [g_bx] bar x
;       [g_bw] filled width  [g_col] bar colour
; =============================================================================
draw_gauge:
    mov eax, SYS_GFX_STRING
    mov ebx, [ebp + g_lx]
    mov ecx, [ebp + g_y]
    mov edx, COL_WHITE
    int 0x80
    mov eax, SYS_GFX_FILLRECT
    mov ebx, [ebp + g_bx]
    mov ecx, [ebp + g_y]
    mov edx, [ebp + g_col]
    mov esi, [ebp + g_bw]
    mov edi, 6
    int 0x80
    ret

; =============================================================================
; gen_sin — build a 256-entry signed sine table scaled to ±256
; Uses Bhaskara I's approximation; P=128 represents pi.
; =============================================================================
gen_sin:
    xor ecx, ecx                       ; i = 0
.gl:
    mov eax, ecx
    and eax, 127                       ; h = i & 127
    mov ebx, eax                       ; h
    mov edx, 128
    sub edx, ebx                       ; 128 - h
    imul ebx, edx                      ; prod = h*(128-h)
    mov eax, ebx
    imul eax, 4096                     ; num = 256*16*prod
    mov edx, ebx
    imul edx, 4                        ; 4*prod
    mov esi, 81920                     ; 5*128*128
    sub esi, edx                       ; den = 81920 - 4*prod
    xor edx, edx
    div esi                            ; eax = num/den  (0..256)
    test ecx, 128
    jz .store
    neg eax                            ; second half of the circle is negative
.store:
    mov [ebp + sine_tab + ecx*2], ax
    inc ecx
    cmp ecx, 256
    jl .gl
    ret

; =============================================================================
; PRNG (LCG) + ranged helpers
; =============================================================================
rand_next:
    mov eax, [ebp + rngstate]
    imul eax, 1103515245
    add eax, 12345
    mov [ebp + rngstate], eax
    ret

rand_x:                                ; -> eax in [-160,159]
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 320
    div ecx
    mov eax, edx
    sub eax, 160
    ret

rand_y:                                ; -> eax in [-120,119]
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 240
    div ecx
    mov eax, edx
    sub eax, 120
    ret

rand_z:                                ; -> eax in [16,255]
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 240
    div ecx
    mov eax, edx
    add eax, 16
    ret

rand_wx:                               ; -> eax in [-1500,1499] (world x)
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 3000
    div ecx
    mov eax, edx
    sub eax, 1500
    ret

rand_wy:                               ; -> eax in [-800,799] (world y)
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 1600
    div ecx
    mov eax, edx
    sub eax, 800
    ret

rand_wz:                               ; -> eax in [1800,3799] (world z)
    call rand_next
    shr eax, 16
    and eax, 0x7FFF
    xor edx, edx
    mov ecx, 2000
    div ecx
    mov eax, edx
    add eax, 1800
    ret

; =============================================================================
; GALAXY — procedural system generation (classic Elite seed-twist)
; =============================================================================
; twist_work — advance the working seeds w0/w1/w2 (16-bit wrap)
twist_work:
    push eax
    push ebx
    mov eax, [ebp + w0]
    add eax, [ebp + w1]
    add eax, [ebp + w2]
    and eax, 0xFFFF
    mov ebx, [ebp + w1]
    mov [ebp + w0], ebx
    mov ebx, [ebp + w2]
    mov [ebp + w1], ebx
    mov [ebp + w2], eax
    pop ebx
    pop eax
    ret

; twist_master — advance the persistent galaxy seeds seed0/1/2
twist_master:
    push eax
    push ebx
    mov eax, [ebp + seed0]
    add eax, [ebp + seed1]
    add eax, [ebp + seed2]
    and eax, 0xFFFF
    mov ebx, [ebp + seed1]
    mov [ebp + seed0], ebx
    mov ebx, [ebp + seed2]
    mov [ebp + seed1], ebx
    mov [ebp + seed2], eax
    pop ebx
    pop eax
    ret

; hyperspace — jump to the next system in the galaxy sequence
hyperspace:
    call twist_master
    call twist_master
    call twist_master
    call twist_master
    call compute_system
    ret

; =============================================================================
; compute_system — derive name/economy/government/tech/pop/prod from the
; current galaxy seeds into the sys_* fields. Works on a copy so the master
; seeds stay anchored to this system.
; =============================================================================
compute_system:
    pushad
    cli
    mov eax, [ebp + seed0]
    mov [ebp + w0], eax
    mov eax, [ebp + seed1]
    mov [ebp + w1], eax
    mov eax, [ebp + seed2]
    mov [ebp + w2], eax

    ; longnameflag = w0 & 64
    mov eax, [ebp + w0]
    and eax, 64
    mov [ebp + longname], eax

    ; x = (w1>>8)&0xFF ; y = (w0>>8)&0xFF
    mov eax, [ebp + w1]
    shr eax, 8
    and eax, 0xFF
    mov [ebp + sys_x], eax
    mov eax, [ebp + w0]
    shr eax, 8
    and eax, 0xFF
    mov [ebp + sys_y], eax

    ; govtype = (w1>>3)&7
    mov eax, [ebp + w1]
    shr eax, 3
    and eax, 7
    mov [ebp + sys_gov], eax

    ; economy = (w0>>8)&7 ; if gov<=1 economy |= 2
    mov eax, [ebp + w0]
    shr eax, 8
    and eax, 7
    cmp dword [ebp + sys_gov], 1
    jg .econ_ok
    or eax, 2
.econ_ok:
    mov [ebp + sys_econ], eax

    ; techlev = ((w1>>8)&3) + (economy^7) + (gov>>1) + (gov&1)
    mov eax, [ebp + w1]
    shr eax, 8
    and eax, 3
    mov ecx, [ebp + sys_econ]
    xor ecx, 7
    add eax, ecx
    mov ecx, [ebp + sys_gov]
    mov edx, ecx
    shr edx, 1
    add eax, edx
    and ecx, 1
    add eax, ecx
    mov [ebp + sys_tech], eax

    ; population = tech*4 + economy + gov + 1
    mov eax, [ebp + sys_tech]
    shl eax, 2
    add eax, [ebp + sys_econ]
    add eax, [ebp + sys_gov]
    inc eax
    mov [ebp + sys_pop], eax

    ; productivity = ((economy^7)+3)*(gov+4)*population*8
    mov eax, [ebp + sys_econ]
    xor eax, 7
    add eax, 3
    mov ebx, [ebp + sys_gov]
    add ebx, 4
    imul eax, ebx
    imul eax, [ebp + sys_pop]
    shl eax, 3
    mov [ebp + sys_prod], eax

    call gen_name
    sti
    popad
    ret

; gen_name — build the system name into sysname (3 digrams, +1 if long)
gen_name:
    lea edi, [ebp + sysname]
    xor ecx, ecx                       ; i
.nl:
    cmp ecx, 4
    jge .done
    mov eax, [ebp + w2]
    shr eax, 8
    and eax, 31
    shl eax, 1                         ; pair = idx*2
    mov [ebp + pairidx], eax
    push ecx
    call twist_work
    pop ecx
    cmp ecx, 3
    jl .append
    cmp dword [ebp + longname], 0
    je .skip
.append:
    mov eax, [ebp + pairidx]
    mov bl, [ebp + pairs + eax]
    cmp bl, '.'
    je .c2
    mov [edi], bl
    inc edi
.c2:
    mov eax, [ebp + pairidx]
    mov bl, [ebp + pairs + eax + 1]
    cmp bl, '.'
    je .skip
    mov [edi], bl
    inc edi
.skip:
    inc ecx
    jmp .nl
.done:
    mov byte [edi], 0
    ret

; =============================================================================
; num_to_buf — write eax as decimal at [edi], advance edi past last digit.
;   Preserves eax; leaves edi at the new end (no NUL written).
; =============================================================================
num_to_buf:
    push eax
    push ebx
    push ecx
    push edx
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .l
    mov byte [edi], '0'
    inc edi
    jmp .done
.l:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .l
.emit:
    pop edx
    add dl, '0'
    mov [edi], dl
    inc edi
    loop .emit
.done:
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; append_str — copy NUL-terminated string at esi into [edi]; edi advanced.
append_str:
    push eax
.l:
    mov al, [esi]
    test al, al
    jz .d
    mov [edi], al
    inc esi
    inc edi
    jmp .l
.d:
    pop eax
    ret

; print_num — draw eax as decimal at (ebx=x, ecx=y) in colour edx
print_num:
    pushad
    mov [ebp + pn_x], ebx
    mov [ebp + pn_y], ecx
    mov [ebp + pn_col], edx
    lea edi, [ebp + numbuf]
    call num_to_buf
    mov byte [edi], 0
    lea esi, [ebp + numbuf]
    mov ebx, [ebp + pn_x]
    mov ecx, [ebp + pn_y]
    mov edx, [ebp + pn_col]
    mov eax, SYS_GFX_STRING
    int 0x80
    popad
    ret

; row_label — esi=label string, draw at (16,ecx) green (ecx = y passed in cl_y)
; (inlined per call below; no shared helper needed)

; =============================================================================
; draw_data_screen — the f6 "Data on System" view
; =============================================================================
draw_data_screen:
    mov eax, SYS_GFX_CLEAR
    mov ebx, COL_BLACK
    int 0x80

    ; title:  SYSTEM: <name>
    lea esi, [ebp + s_dtitle]
    mov ebx, 16
    mov ecx, 8
    mov edx, COL_TITLE
    mov eax, SYS_GFX_STRING
    int 0x80
    lea esi, [ebp + sysname]
    mov ebx, 90
    mov ecx, 8
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80
    mov eax, SYS_GFX_LINE
    mov ebx, 16
    mov ecx, 20
    mov edx, 304
    mov esi, 20
    mov edi, COL_TITLE
    int 0x80

    ; ECONOMY
    lea esi, [ebp + s_decon]
    mov ebx, 16
    mov ecx, 36
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    mov eax, [ebp + sys_econ]
    mov esi, [ebp + econ_tbl + eax*4]
    lea esi, [ebp + esi]
    mov ebx, 130
    mov ecx, 36
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; GOVERNMENT
    lea esi, [ebp + s_dgov]
    mov ebx, 16
    mov ecx, 52
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    mov eax, [ebp + sys_gov]
    mov esi, [ebp + gov_tbl + eax*4]
    lea esi, [ebp + esi]
    mov ebx, 130
    mov ecx, 52
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; TECH LEVEL
    lea esi, [ebp + s_dtech]
    mov ebx, 16
    mov ecx, 68
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    mov eax, [ebp + sys_tech]
    inc eax
    mov ebx, 130
    mov ecx, 68
    mov edx, COL_WHITE
    call print_num

    ; POPULATION (pop/10 . pop%10 Billion)
    lea esi, [ebp + s_dpop]
    mov ebx, 16
    mov ecx, 84
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    lea edi, [ebp + linebuf]
    mov eax, [ebp + sys_pop]
    xor edx, edx
    mov ebx, 10
    div ebx                            ; eax=int part, edx=frac
    push edx
    call num_to_buf
    mov byte [edi], '.'
    inc edi
    pop edx
    add dl, '0'
    mov [edi], dl
    inc edi
    lea esi, [ebp + s_billion]
    call append_str
    mov byte [edi], 0
    lea esi, [ebp + linebuf]
    mov ebx, 130
    mov ecx, 84
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; PRODUCTIVITY
    lea esi, [ebp + s_dprod]
    mov ebx, 16
    mov ecx, 100
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    lea edi, [ebp + linebuf]
    mov eax, [ebp + sys_prod]
    call num_to_buf
    lea esi, [ebp + s_mcr]
    call append_str
    mov byte [edi], 0
    lea esi, [ebp + linebuf]
    mov ebx, 130
    mov ecx, 100
    mov edx, COL_WHITE
    mov eax, SYS_GFX_STRING
    int 0x80

    ; COORDINATES
    lea esi, [ebp + s_dcoord]
    mov ebx, 16
    mov ecx, 116
    mov edx, COL_GREEN
    mov eax, SYS_GFX_STRING
    int 0x80
    mov eax, [ebp + sys_x]
    mov ebx, 130
    mov ecx, 116
    mov edx, COL_WHITE
    call print_num
    mov eax, [ebp + sys_y]
    mov ebx, 180
    mov ecx, 116
    mov edx, COL_WHITE
    call print_num

    ; footer
    lea esi, [ebp + s_dfoot]
    mov ebx, 16
    mov ecx, 184
    mov edx, COL_CYAN
    mov eax, SYS_GFX_STRING
    int 0x80
    ret

; =============================================================================
; Data
; =============================================================================
; Cobra Mk III (simplified): flat arrow with a raised cockpit spine.
verts:
    db   0,  0,  40        ; 0 nose
    db -40,  0, -30        ; 1 left wing tip
    db  40,  0, -30        ; 2 right wing tip
    db -12,  0, -30        ; 3 tail left
    db  12,  0, -30        ; 4 tail right
    db   0, 16,  -6        ; 5 cockpit top

edges:
    db 0, 1
    db 0, 2
    db 1, 3
    db 2, 4
    db 3, 4
    db 0, 5
    db 5, 3
    db 5, 4

s_fs     db "FS", 0
s_as     db "AS", 0
s_fu     db "FU", 0
s_ct     db "CT", 0
s_lt     db "LT", 0
s_al     db "AL", 0
s_sp     db "SP", 0
s_en     db "EN", 0
s_elite  db "ELITE", 0
s_title  db "Front View", 0
s_kills  db "KILLS", 0

; ── System Data screen text ──────────────────────────────────────────────────
s_dtitle db "SYSTEM:", 0
s_decon  db "ECONOMY:", 0
s_dgov   db "GOVERNMENT:", 0
s_dtech  db "TECH LEVEL:", 0
s_dpop   db "POPULATION:", 0
s_dprod  db "PRODUCTIVITY:", 0
s_dcoord db "GAL X / Y:", 0
s_billion db " BILLION", 0
s_mcr    db " M CR", 0
s_dfoot  db "N-HYPERSPACE  F-FLIGHT  Q-QUIT", 0

s_g0 db "ANARCHY", 0
s_g1 db "FEUDAL", 0
s_g2 db "MULTI-GOV", 0
s_g3 db "DICTATORSHIP", 0
s_g4 db "COMMUNIST", 0
s_g5 db "CONFEDERACY", 0
s_g6 db "DEMOCRACY", 0
s_g7 db "CORPORATE STATE", 0

s_e0 db "RICH INDUSTRIAL", 0
s_e1 db "AVERAGE INDUSTRIAL", 0
s_e2 db "POOR INDUSTRIAL", 0
s_e3 db "MAINLY INDUSTRIAL", 0
s_e4 db "MAINLY AGRICULTURAL", 0
s_e5 db "RICH AGRICULTURAL", 0
s_e6 db "AVERAGE AGRICULTURAL", 0
s_e7 db "POOR AGRICULTURAL", 0

gov_tbl  dd s_g0, s_g1, s_g2, s_g3, s_g4, s_g5, s_g6, s_g7
econ_tbl dd s_e0, s_e1, s_e2, s_e3, s_e4, s_e5, s_e6, s_e7

; Two-letter digram table for procedural names ('.' = skip)
pairs    db "..LEXEGEZACEBISOUSESARMAINDIREA.ERATENBERALAVETIEDORQUANTEISRION"

; ── Writable state ──────────────────────────────────────────────────────────
sine_tab times 256 dw 0
proj_x   times NVERTS dd 0
proj_y   times NVERTS dd 0
pvalid   times NVERTS db 0
stars    times (NSTARS*3) dd 0

angX     dd 0
angY     dd 0
sinX     dd 0
cosX     dd 0
sinY     dd 0
cosY     dd 0

tx       dd 0
ty       dd 0
tz       dd 0
ty1      dd 0
tz1      dd 0
tx2      dd 0
tz2      dd 0
tzc      dd 0

rvx      dd 0
rvy      dd 0
rvz      dd 0
rvz1     dd 0
ccx      dd 0
ccy      dd 0
ccz      dd 0
camx     dd 0
camy     dd 0
camz     dd 0
st_z     dd 0
vidx     dd 0
blip_x   dd 0
blip_y   dd 0

speed       dd 0
energy      dd 0
kills       dd 0
laser_timer dd 0
firereq     db 0

shp_x     times NSHIPS dd 0
shp_y     times NSHIPS dd 0
shp_z     times NSHIPS dd 0
shp_sx    times NSHIPS dd 0
shp_sy    times NSHIPS dd 0
shp_cz    times NSHIPS dd 0
shp_alive times NSHIPS db 0
shp_exp   times NSHIPS db 0
shp_on    times NSHIPS db 0
shp_i     dd 0

ei       dd 0
ea       dd 0
eb       dd 0
sidx     dd 0
soff     dd 0
sxv      dd 0
syv      dd 0

ce_x     dd 0
ce_y     dd 0
ce_rx    dd 0
ce_ry    dd 0
ce_col   dd 0
ce_step  dd 0
ce_a     dd 0
ce_nx    dd 0
ce_ny    dd 0
ce_px    dd 0
ce_py    dd 0
ce_fx    dd 0
ce_fy    dd 0

g_lx     dd 0
g_y      dd 0
g_bx     dd 0
g_bw     dd 0
g_col    dd 0

; galaxy / system state
gmode    db 0
seed0    dd 0
seed1    dd 0
seed2    dd 0
w0       dd 0
w1       dd 0
w2       dd 0
longname dd 0
pairidx  dd 0
sys_x    dd 0
sys_y    dd 0
sys_gov  dd 0
sys_econ dd 0
sys_tech dd 0
sys_pop  dd 0
sys_prod dd 0
pn_x     dd 0
pn_y     dd 0
pn_col   dd 0
sysname  times 16 db 0
numbuf   times 16 db 0
linebuf  times 48 db 0

rngstate dd 0
last_tick dd 0
quitf    db 0
