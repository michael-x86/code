section .data

tick_flag    db 0
tick_count   dd 0
tick_div     dd 0
cursor_pos   dd 0
prompt_limit dd 0
cmd_pos      dd 0      
gfx_mode     db 0      ; 0 = text mode, 1 = 800x400 graphics
mode13       db 0      ; 1 = VGA mode 0x13 (320x200x256) active
gfx_mapped   db 0     
banner_mode  db 0     
lfb_phys     dd 0      

mandel_colors:
    dd 0x00000764
    dd 0x00001E96
    dd 0x000048C8
    dd 0x000082D8
    dd 0x0000C0E0
    dd 0x0020E0B0
    dd 0x0060F070
    dd 0x00A8F840
    dd 0x00E0F020
    dd 0x00F8C000
    dd 0x00F88800
    dd 0x00F04800
    dd 0x00D01030
    dd 0x00A00880
    dd 0x006018C0
    dd 0x004008A0
mandel_colors_end:
MAND_NCOL equ (mandel_colors_end - mandel_colors) / 4

; --- Mandelbrot view (Q16.16)
mand_cx   dd -49040
mand_cy   dd 0
mand_step dd 287

font_saved   db 0  

; mandel_simd (SSE) constants: packed single floats
align 16
sv_lanestep dd 0.0, 0.004375, 0.00875, 0.013125  ; step * [0,1,2,3]
sv_four     dd 4.0, 4.0, 4.0, 4.0                ; escape radius^2
sv_two      dd 2.0, 2.0, 2.0, 2.0
sv_one      dd 1.0, 1.0, 1.0, 1.0
sf_xmin     dd -2.5
sf_ymin     dd -0.875
sf_step     dd 0.004375      ; 3.5/800 = 1.75/400 (square)
    

login_user_msg db "Login: ",0
login_pass_msg db "Password: ",0
login_ok_msg   db "Access granted",13,0
login_bad_msg  db "Login incorrect",13,0
login_root     db "aug6",0       ;NEVER AGAIN!
login_secret   db "1945",0
login_shift    db 0     
login_echo     db 0              ;1=echo, 0=mask *

;--- buffers ---
kbd_head     dd 0
kbd_tail     dd 0

out_mem         db "OUT OF MEMORY - KEEP DREAMING",13,0
too_many_msg    db 13,"too many processes",13,0
bg_lbl          db "[bg pid ",0
bg_lbl2         db "]",13,0
alloc_mem       db "heap pointer  : 0x",0
no_arg          db 13,'usage: alloc <bytes>',13,0
poke_msg        db 13,"usage: poke <hex addr> <hex value>",13,0
peek_msg        db 13,"usage: peek <hex addr>",13,0
plot_msg        db 13,"usage: plot x y (0-79 x 0-24)" ,13,0   
free_usage_msg  db 13,"usage: free <hex addr>",13,0
free_ok_msg     db "memory released",13,0
bad_free_msg    db "invalid allocation",13,0
pf_msg          db 13,"PAGE FAULT",13,0
pf_addr         db "ADDRESS: 0x",0
command_nf_msg   db 13,"command not found",13,0
run_file         db "exec.cmd",0
run_nofile_msg   db "exec.cmd not found",13,0

hz                     dd 100,0
kernel_phys_start_var: dd 0
kernel_phys_end_var:   dd 0

boot_epoch             dd 0   ; system baseline clock
month_days:            db 31,28,31,30,31,30,31,31,30,31,30,31
sec     dd 0
min     dd 0
hour    dd 0
day     dd 0
month   dd 0
year    dd 0
dec_buf times 11 db 0

bin_prefix   db "/bin/",0
exec_vbase   dd 0
exec_pages   dd 0

argc         dd 0
argv times 16 dd 0
bg_flag      dd 0    ; 1 = exec_bin in background (trailing '&')
detach_req   db 0    ; 1 = running process to self-background (sys_detach)

cmd_len      dd 0

section .data
current_task:  dd 0
task_esps:                           ; saved kernel ESP per process
    task0_esp: dd 0
    task1_esp: dd 0
    task2_esp: dd 0
    dd 0, 0, 0, 0, 0                 ; slots 3..MAX_PROC-1
proc_state:                          ; 0 = free, 1 = active (schedulable)
    dd 1, 1, 1, 0, 0, 0, 0, 0
proc_vbase:                          ; binary virtual base, for cleanup (0 = none)
    dd 0, 0, 0, 0, 0, 0, 0, 0
proc_pages:                          ; binary page count, for cleanup (0 = none)
    dd 0, 0, 0, 0, 0, 0, 0, 0
proc_name:                           ; for ps
    times MAX_PROC*PROC_NAME_LEN db 0

hist_count  dd 0
hist_index  dd 0

;---- INTERRUPT DESC TABLE ----
idt_descriptor:
            dw idt_end-idt_start-1
            dd idt_start

;---- SYSCALL TABLE  (index = eax at int 0x80) ----
syscall_table:
    dd sys_exit          ; 0 : eax = return code
    dd sys_print         ; 1 : esi = string ptr
    dd sys_print_cr      ; 2 : esi = string ptr (CR aware)
    dd sys_newline       ; 3
    dd sys_cls           ; 4
    dd sys_print_hex     ; 5 : ebx = value out=dword
    dd sys_print_int     ; 6 : ebx = value
    dd sys_get_key       ; 7 : -> eax = ascii (0 if none)
    dd sys_get_tick      ; 8 : -> eax = tick_count
    dd sys_shutdown      ; 9
    dd sys_read_mem      ; 10: ebx = addr -> eax = dword at [addr]
    dd sys_getcwd        ; 11: edi = dst
    dd sys_chdir         ; 12: esi = path 
    dd sys_list_dir      ; 13: ebx = idx, edi = dst 
    dd sys_get_arg       ; 14: ebx = idx, edi = dst 
    dd sys_stat          ; 15: esi = path,edi = info(12B) 
    dd sys_print_n       ; 16: esi = ptr, ecx = count 
    dd sys_create        ; 17: esi = path 
    dd sys_write         ; 18: esi = path,ebx=buf,ecx=n
    dd sys_unlink        ; 19: esi = path 
    dd sys_mkdir         ; 20: esi = path  
    dd sys_rmdir         ; 21: esi = path 
    dd sys_ps_info       ; 22: ebx = dst ptr 
    dd sys_stack_dump    ; 23: print top of stack
    dd sys_alloc         ; 24: in = <bytes> -> page ptr 
    dd sys_dealloc       ; 25: in = <page ptr> 
    dd sys_peek          ; 26: in = <address> 
    dd sys_poke          ; 27: in = <address> <value> 
    dd sys_hex2int       ; 28: in = esi out = eax
    dd sys_banner        ; 29: print banner
    dd sys_dydx          ; 30: Test
    dd sys_out_hex       ; 31: in = eax out=print hex word 
    dd sys_mem_dump      ; 32: in = esi -> address. out: print 64 bytes 
    dd sys_hex_byte      ; 33: print hex byte
    dd sys_asc2int       ; 34: in = esi->string out=edx
    dd sys_hertz         ; 35: print current hertz
    dd sys_tick          ; 36: print heartbeats
    dd sys_plot          ; 37: set block at ecx edx 
    dd sys_epoch         ; 38: out: prints unixtime 
    dd sys_putchar       ; 39: ebx = char
    dd sys_kill          ; 40: ebx = PID to terminate
    dd sys_reg_dump      ; 41: print regs
    dd sys_heap_info     ; 42: ebx = dst -> heap alloc table snapshot
    dd sys_detach        ; 43: background the calling process (return to prompt)
    dd sys_setfreq       ; 44: set PIT frequency from argv[1] (Hz)
    dd sys_banner_flip   ; 45: Test
    dd sys_mode13        ; 46: enter VGA mode 13h
    dd sys_setpixel      ; 47: ebx=x ecx=y edx=colour -> plot pixel
    dd sys_clearpixel    ; 48: ebx=x ecx=y -> clear pixel
    dd sys_gclear        ; 49: clear the mode-13h framebuffer
    dd sys_run           ; 50: run commands from exec.cmd one by one
    dd sys_append        ; 51: esi=path,ebx=buf,ecx=n -> append to a file
SYSCALL_COUNT equ ($-syscall_table)/4

;---- Keycode -> ASCII Convertion ----
kbd_shift db 0

