sys_msg   db "*** x86 Operating System ***", 0

deadbeef  db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF
          db 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF 

help_lbl db 13," ---     BuzyBox     ---",13,13
        db "peek  -  at 16 bytes @ esi",13
        db "regs  -  cpu registers",13
        db "stack -  top of stack",13
        db "alloc -  4KB chunks",13
        db "free  -  relase a chunk",13
        db "heap  -  current heap",13
        db "space -  virtual memory",13
        db "clear -  clear screen",13
        db "echo  -  echo <argument>",13
        db "sys   -  int 0x80 (syscall)",13
        db "exit  -  shutdown",13,13
        db 0

eax_lbl db "EAX: ",0
ebx_lbl db "EBX: ",0
ecx_lbl db "ECX: ",0
edx_lbl db "EDX: ",0
esi_lbl db "ESI: ",0
edi_lbl db "EDI: ",0
ebp_lbl db "EBP: ",0
esp_lbl db "ESP: ",0

tick_flag    db 0
tick_count   dd 0      
tick_div     dd 0     
cursor_pos   dd 0 
prompt_limit dd 0

;--- buffers ---
kbd_head     dd 0
kbd_tail     dd 0
; kbd_buf in .bss

;--- cmd ---
out_mem   db "OUT OF MEMORY - KEEP DREAMING",13,0
alloc_mem db "heap pointer  : 0x",0
no_arg    db 13,'usage: alloc <size>',13,0

in_bytes        db " bytes free",13,0
free_usage_msg  db 13,"usage: free <hexaddr>",13,0
free_ok_msg     db "memory released",13,0
bad_free_msg    db "invalid allocation",13,0

pf_msg db 13,"PAGE FAULT",13,0
pf_addr db "ADDRESS: 0x",0

virt_mem: db "virtual : $",0
real_mem: db "reality : $",0

sys_peek_msg db "peek = 0x",0

bin_prefix   db "/bin/",0
;ata_drive    db 0  ; 0=master 16=slave
cmd_nf_msg   db 13,"command not found",13,0

argc        dd 0
argv        times 16 dd 0

cmd_len     dd 0
cmd_exec    dd 0
; cmd_buf in .bss

hist_count  dd 0
hist_index  dd 0
; hist_buf in .bss

;---- INTERRUPT DESC TABLE ----
; idt_start / idt_end in .bss
idt_descriptor:
    dw idt_end-idt_start-1
    dd idt_start

;---- SYSCALL TABLE  (index = eax at int 0x80) ----
syscall_table:
    dd sys_putchar       ; 0 : ebx = char
    dd sys_print         ; 1 : esi = string ptr
    dd sys_print_cr      ; 2 : esi = string ptr (CR aware)
    dd sys_newline       ; 3
    dd sys_cls           ; 4
    dd sys_print_hex     ; 5 : ebx = value
    dd sys_print_int     ; 6 : ebx = value
    dd sys_get_key       ; 7 : -> eax = ascii (0 if none)
    dd sys_get_tick      ; 8 : -> eax = tick_count
    dd sys_shutdown      ; 9
    dd sys_read_mem      ; 10: ebx = addr -> eax = dword at [addr]
    dd sys_getcwd        ; 11: edi = dst
    dd sys_chdir         ; 12: esi = path -> eax = 0/-1
    dd sys_list_dir      ; 13: ebx = idx, edi = dst -> eax = type/-1
    dd sys_get_arg       ; 14: ebx = idx, edi = dst -> eax = 0/-1
    dd sys_stat          ; 15: esi = path, edi = info(12B) -> eax = 0/-1
    dd sys_print_n       ; 16: esi = ptr, ecx = count -> eax = 0
    dd sys_create        ; 17: esi = path -> eax = 0/-1
    dd sys_write         ; 18: esi = path, ebx = buf, ecx = n -> eax = 0/-1
    dd sys_unlink        ; 19: esi = path -> eax = 0/-1
SYSCALL_COUNT equ ($-syscall_table)/4

;---- Keycode -> ASCII Convertion ----
section .rodata
keymap:
    db 0,27,'1','2','3','4','5','6','7','8'
    db '9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i'
    db 'o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k'
    db 'l',';',39,'`',0,'\'
    db 'z','x','c','v','b','n','m'
    db ',','.','/',0,'*',0,' '
    times 0x3B-($-keymap) db 0     ; F1
    db '<'
    times 256-($-keymap) db 0

keymap_shift:
    db 0,27,'!','@','#','$','%','^','&','*'
    db '(' ,')','_','+',8,9
    db 'Q','W','E','R','T','Y','U','I'
    db 'O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K'
    db 'L',':','"', '~',0,'|'
    db 'Z','X','C','V','B','N','M'
    db '<','>','?',0,'*',0,' '
    times 0x3B-($-keymap_shift) db 0     ; F1
    db '<'
    times 256-($-keymap_shift) db 0

; --------------------------------------------------
; format: db "command",0 
;         dd address 
; final   db 0
; --------------------------------------------------
cmd_table:             ; BBox Cmds
    db "peek",0
    dd peek_cmd
    db "regs",0
    dd show_regs
    db "stack",0
    dd show_stack
    db "clear",0
    dd cls
    db "quit",0
    dd shutdown
    db "exit",0
    dd shutdown
    db "help",0
    dd help_cmd
    db "echo",0
    dd echo_cmd
    db "alloc",0
    dd alloc_cmd
    db "free",0
    dd free_cmd
    db "heap",0
    dd heap_cmd
    db "space",0
    dd space_cmd
    db "sys",0
    dd sys_cmd
    db 0          ; end of BuzyBox

;---- in-kernel virtual filesystem ----
%include "fs.inc"

section .bss
;----------------------------
alignb 16

tab_match_count  resd 1
tab_single_ptr   resd 1

task0_esp    resd 1
task1_esp    resd 1
task2_esp    resd 1
current_task resd 1

alignb 4
kbd_buf      resb 256
cmd_buf      resb 64
hist_buf     resb 32*64
dir_buf      resb 512

;---- VFS state ----
cwd_buf      resb 128
resolve_buf  resb 128
path_buf     resb 128
tmp_dst      resd 1
tmp_left     resd 1
persist_buf  resb 1536    ; 3 sectors: metadata + 1024 B content

alignb 8
idt_start:
    resb 256*8
idt_end:

alignb 16
task0_stack:
    resb 4096
task0_stack_top:

alignb 16
task1_stack:
    resb 4096
task1_stack_top:

alignb 16
task2_stack:
    resb 4096
task2_stack_top:

;-------------------------------

;---- STACK ----
alignb 16
stack_bottom:
    resb 16384      ; 16 KB stack
stack_top:

alignb 4096
page_directory:
    resd 1024

alignb 4096
page_table_0:
    resd 1024

alignb 4096
first_page_table:
    resd 1024

alignb 4096
second_page_table:
    resd 1024

alignb 4096
page_table_1:
    resd 1024

alignb 4096
page_table_2:
    resd 1024

alignb 4
heap_start:
    resb 1024*1024     ;  1 MB heap
heap_end:

alignb 4
page_bitmap:
    resb 32768

alloc_table_count equ 128

alloc_table:
; entry format:
; +0  virtual address
; +4  page count

resd alloc_table_count*2
