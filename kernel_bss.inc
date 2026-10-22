section .bss
;----------------------------
alignb 16

tab_match_count  resd 1
tab_single_ptr   resd 1
tab_pfx          resd 1   
tab_pfxlen       resd 1   
tab_mode         resb 1   
tab_printing     resb 1    

alignb 4
kbd_buf          resb 256
login_ubuf       resb 64
login_pbuf       resb 64
cmd_buf          resb 64
run_ptr          resd 1        ; exec.cmd read cursor
run_rem          resd 1        ; exec.cmd bytes remaining
hist_buf         resb 32*64
dir_buf          resb 512

;---- VFS state ----
cwd_buf          resb 128
resolve_buf      resb 128
path_buf         resb 128
tmp_dst          resd 1
tmp_left         resd 1
persist_buf      resb 2560    ; 5 sectors: metadata+2048 Bytes

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

alignb 16
proc_stacks:        ; stacks for spawned processes (slots 3..MAX_PROC-1)
    resb (MAX_PROC-3)*4096
proc_stacks_top:


;---- STACK ----
alignb 16
stack_bottom:
    resb 16384      ; 16 KB stack  (256 bytes in C64)
stack_top:

alignb 4096
page_directory:
    resd 1024

alignb 4096
identity_page_table:
    resd 1024

alignb 4096
kernel_low_page_table: 
    resd 1024

alignb 4096
heap_page_table_0:
    resd 1024

alignb 4096
heap_page_table_1:
    resd 1024

alignb 4096
heap_page_table_2:
    resd 1024

alignb 4096
gfx_page_table:            ; maps the 800x400 LFB at LFB_VIRT (PDE 773)
    resd 1024

alignb 4
m_zr  resd 1               ; Mandelbrot Q16.16 working values
m_zi  resd 1
m_cr  resd 1
m_ci  resd 1
m_zr2 resd 1
m_zi2 resd 1
m_x0  resd 1               ; view tlc (derived from centre/scale)
m_y0  resd 1


alignb 16   
simd_iter_buf resd 4       ; 4 packed iteration counts (movaps target)

alignb 4
font_save:                 ; VGA text font (plane 2, 8 KB)
    resb 8192

dac_save:                  ; text-mode DAC palette (256*RGB)
    resb 768

alignb 4
heap_start:
    resb 12*1024*1024    
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

