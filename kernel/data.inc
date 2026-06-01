; =============================================================================
; data.inc — mutable global state (initialized, emitted in binary)
; =============================================================================

section .data

; --- Mutable scalars ---
tick_flag    db 0
tick_count   dd 0
tick_div     dd 0
cursor_pos   dd 0
prompt_limit dd 0

kbd_shift    db 0

; --- Active keymap pointers (set from /etc/config at boot) --------------------
keymap_active      dd keymap
keymap_shift_active dd keymap_shift

; --- Config state (populated from /etc/config) -------------------------------
cfg_timezone    dd 0          ; timezone offset in hours from UTC (-12..+14)
cfg_datefmt     dd 0          ; 0=YYYY-MM-DD, 1=DD/MM/YYYY, 2=MM/DD/YYYY
cfg_timefmt     dd 0          ; 0=24h, 1=12h
cfg_layout      dd 0          ; 0=US, 1=Swedish

kernel_phys_start_var: dd 0
kernel_phys_end_var:   dd 0

exec_vbase   dd 0
exec_pages   dd 0
exec_name    times 32 db 0

argc         dd 0
argv         times ARGV_MAX dd 0

cmd_len      dd 0
cmd_exec     dd 0

hist_count   dd 0
hist_index   dd 0

; --- IDT descriptor (loaded by lidt) ----------------------------------------
idt_descriptor:
    dw idt_end - idt_start - 1
    dd idt_start

; --- Syscall jump table ------------------------------------------------------
syscall_table:
    dd sys_putchar
    dd sys_print
    dd sys_print_cr
    dd sys_newline
    dd sys_cls
    dd sys_print_hex
    dd sys_print_int
    dd sys_get_key
    dd sys_get_tick
    dd sys_shutdown
    dd sys_read_mem
    dd sys_getcwd
    dd sys_chdir
    dd sys_list_dir
    dd sys_get_arg
    dd sys_stat
    dd sys_print_n
    dd sys_create
    dd sys_write
    dd sys_unlink
    dd sys_mkdir
    dd sys_rmdir
    dd sys_get_ps_info
    dd sys_alloc_pages
    dd sys_free_pages
    dd sys_write_mem
    dd sys_hex2int
    dd sys_asc2int
    dd sys_get_proc_info
    dd sys_itoa
    dd sys_get_config
