; debug — interactive debugger for inspecting kernel state
; Usage: debug
;
; Features:
;   - Shows process info (task ID, ESP values)
;   - Shows current tick count
;   - Escape key shows register/debug page
;   - 'm' key to examine memory
;   - 'q' key to quit
;   - PgUp/PgDown to scroll in debug info page
;
; Keys:
;   ESC/d  - toggle debug info page
;   m      - enter memory examination mode
;   q      - quit debugger
;   r      - refresh display
;   +/-    - scroll up/down (in debug info page)
;   Note: PgUp/PgDown are consumed by kernel for shell scrolling

[bits 32]
[org 0x00000000]

%include "userland.inc"

; -----------------------------------------------------------------------------
; Constants
; -----------------------------------------------------------------------------
KEY_ESC        equ 0x1B
KEY_M          equ 'm'
KEY_Q          equ 'q'
KEY_R          equ 'r'
KEY_D          equ 'd'          ; Alternative toggle for debug page
; Note: PgUp/PgDown (0xF0/0xF1) are consumed by kernel's get_key
; and cannot be received by userland programs. We implement
; scrolling by tracking internal state instead.

VIDEO_SEGMENT  equ 0xC00B8000
SCREEN_WIDTH   equ 80
SCREEN_HEIGHT  equ 25

; Scroll state for debug info page
MAX_SCROLL     equ 100         ; Maximum scroll lines

; -----------------------------------------------------------------------------
; Entry point
; -----------------------------------------------------------------------------
_start:
    USERLAND_START
    
    ; Clear screen
    mov eax, 4
    int 0x80
    
    ; Main loop
.main_loop:
    call draw_main_screen
    
.wait_key:
    mov eax, 7              ; sys_get_key
    int 0x80
    test eax, eax
    jz .wait_key            ; No key, keep waiting
    
    ; Check which key
    cmp eax, KEY_ESC
    je .show_debug_page
    cmp eax, KEY_D
    je .show_debug_page
    cmp eax, KEY_Q
    je .quit
    cmp eax, KEY_M
    je .memory_mode
    cmp eax, KEY_R
    je .main_loop           ; Refresh
    
    jmp .wait_key

; -----------------------------------------------------------------------------
; Show debug info page (register/process info)
; -----------------------------------------------------------------------------
.show_debug_page:
    mov dword [ebp + scroll_offset], 0    ; Reset scroll on entry
    
.show_debug_page_loop:
    call show_debug_info
    
    ; Wait for key (ESC to return, PgUp/PgDown to scroll)
.dbg_wait_key:
    mov eax, 7
    int 0x80
    test eax, eax
    jz .dbg_wait_key
    
    cmp eax, KEY_ESC
    je .main_loop
    cmp eax, '+'
    je .scroll_up
    cmp eax, '-'
    je .scroll_down
    jmp .dbg_wait_key
    
.scroll_up:
    mov eax, [ebp + scroll_offset]
    test eax, eax
    jz .show_debug_page_loop    ; Already at top
    dec eax
    mov [ebp + scroll_offset], eax
    jmp .show_debug_page_loop
    
.scroll_down:
    mov eax, [ebp + scroll_offset]
    cmp eax, MAX_SCROLL
    jae .show_debug_page_loop   ; At bottom
    inc eax
    mov [ebp + scroll_offset], eax
    jmp .show_debug_page_loop

; -----------------------------------------------------------------------------
; Memory examination mode
; -----------------------------------------------------------------------------
.memory_mode:
    call memory_examine_mode
    jmp .main_loop

; -----------------------------------------------------------------------------
; Quit
; -----------------------------------------------------------------------------
.quit:
    mov eax, 3              ; sys_newline
    int 0x80
    ret

; -----------------------------------------------------------------------------
; Draw main screen
; -----------------------------------------------------------------------------
draw_main_screen:
    ; Title
    lea esi, [ebp + title_msg]
    mov eax, 1
    int 0x80
    
    mov eax, 3              ; sys_newline
    int 0x80
    
    ; Show tick count
    lea esi, [ebp + tick_msg]
    mov eax, 1
    int 0x80
    
    mov eax, 8              ; sys_get_tick
    int 0x80
    mov ebx, eax
    mov eax, 6              ; sys_print_int
    int 0x80
    
    mov eax, 3              ; sys_newline
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Show process info
    lea esi, [ebp + proc_msg]
    mov eax, 1
    int 0x80
    
    ; Get process info (sys_get_ps_info - syscall 22)
    lea ebx, [ebp + ps_buffer]
    mov eax, 22
    int 0x80
    cmp eax, -1
    je .ps_failed
    
    ; Print task ID
    lea esi, [ebp + task_id_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + ps_buffer]
    mov eax, 6
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Print task 0 ESP
    lea esi, [ebp + task0_esp_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + ps_buffer + 4]
    mov eax, 5              ; sys_print_hex
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Print task 1 ESP
    lea esi, [ebp + task1_esp_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + ps_buffer + 8]
    mov eax, 5
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Print task 2 ESP
    lea esi, [ebp + task2_esp_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + ps_buffer + 12]
    mov eax, 5
    int 0x80
    mov eax, 3
    int 0x80
    jmp .ps_done
    
.ps_failed:
    lea esi, [ebp + ps_failed_msg]
    mov eax, 1
    int 0x80
    mov eax, 3
    int 0x80
    
.ps_done:
    ; Separator
    mov eax, 3
    int 0x80
    
    ; Show commands
    lea esi, [ebp + commands_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    
    ret

; -----------------------------------------------------------------------------
; Show debug info page
; -----------------------------------------------------------------------------
show_debug_info:
    ; Clear screen
    mov eax, 4
    int 0x80
    
    ; Title
    lea esi, [ebp + debug_title]
    mov eax, 1
    int 0x80
    mov eax, 3
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Get process info
    lea ebx, [ebp + proc_info_buffer]
    mov eax, 28             ; sys_get_proc_info
    int 0x80
    
    ; Show exec_vbase
    lea esi, [ebp + vbase_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + proc_info_buffer]
    mov eax, 5
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Show exec_pages
    lea esi, [ebp + pages_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + proc_info_buffer + 4]
    mov eax, 6
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Show exec_name
    lea esi, [ebp + name_msg]
    mov eax, 1
    int 0x80
    lea esi, [ebp + proc_info_buffer + 8]
    mov eax, 1
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Separator
    mov eax, 3
    int 0x80
    
    ; Get tick count
    lea esi, [ebp + tick_msg]
    mov eax, 1
    int 0x80
    mov eax, 8
    int 0x80
    mov ebx, eax
    mov eax, 6
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Show PS info again
    mov eax, 3
    int 0x80
    lea ebx, [ebp + ps_buffer2]
    mov eax, 22
    int 0x80
    
    lea esi, [ebp + task_id_msg]
    mov eax, 1
    int 0x80
    mov ebx, [ebp + ps_buffer2]
    mov eax, 6
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Prompt to return
    mov eax, 3
    int 0x80
    lea esi, [ebp + press_esc_msg]
    mov eax, 2
    int 0x80
    
    ret

; -----------------------------------------------------------------------------
; Memory examination mode
; -----------------------------------------------------------------------------
memory_examine_mode:
    ; Clear screen
    mov eax, 4
    int 0x80
    
    lea esi, [ebp + mem_title]
    mov eax, 1
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Prompt for address
    lea esi, [ebp + mem_prompt]
    mov eax, 1
    int 0x80
    
    ; For simplicity, examine a few fixed locations
    ; In a full implementation, you'd read user input here
    
    ; Examine kernel base area
    lea esi, [ebp + examining_msg]
    mov eax, 1
    int 0x80
    
    ; Read memory at 0xC0100000 (kernel entry)
    mov ebx, 0xC0100000
    mov eax, 10             ; sys_read_mem
    int 0x80
    mov ebx, eax
    mov eax, 5              ; sys_print_hex
    int 0x80
    mov eax, 3
    int 0x80
    
    ; Read a few more dwords
    mov ecx, 4
    mov ebx, 0xC0100004
.read_loop:
    push ecx
    mov eax, 10
    int 0x80
    mov ebx, eax
    mov eax, 5
    int 0x80
    mov al, ' '
    mov ebx, eax
    mov eax, 0
    int 0x80
    add ebx, 4
    pop ecx
    loop .read_loop
    
    mov eax, 3
    int 0x80
    
    ; Wait for any key
    lea esi, [ebp + press_key_msg]
    mov eax, 2
    int 0x80
    
.wait:
    mov eax, 7
    int 0x80
    test eax, eax
    jz .wait
    
    ret

; -----------------------------------------------------------------------------
; Data section
; -----------------------------------------------------------------------------
section .data

title_msg:        db "=== Kernel Debugger ===", 13, 10, 0
debug_title:      db "=== Debug Info Page ===", 13, 10, 0
mem_title:        db "=== Memory Examine ===", 13, 10, 0

tick_msg:         db "Tick count: ", 0
proc_msg:         db "Process Info:", 13, 10, 0

task_id_msg:      db "Current Task ID: ", 0
task0_esp_msg:    db "Task 0 ESP: ", 0
task1_esp_msg:    db "Task 1 ESP: ", 0
task2_esp_msg:    db "Task 2 ESP: ", 0
ps_failed_msg:    db "Failed to get PS info", 13, 10, 0

vbase_msg:        db "Exec base: ", 0
pages_msg:        db "Exec pages: ", 0
name_msg:         db "Exec name: ", 0

commands_msg:     db "Keys: ESC/d=debug  m=memory  r=refresh  +/-=scroll  q=quit", 13, 0
press_esc_msg:    db "Press ESC to return, +/- to scroll", 13, 0
press_key_msg:    db "Press any key to return", 13, 0

mem_prompt:       db "Examining kernel area:", 13, 10, 0
examining_msg:    db "0xC0100000: ", 0

; -----------------------------------------------------------------------------
; BSS section
; -----------------------------------------------------------------------------
section .bss
alignb 4

ps_buffer:        resb 16
ps_buffer2:       resb 16
proc_info_buffer: resb 36
scroll_offset:    resd 1              ; Scroll position for debug info page
arg_buf:          resb 32
