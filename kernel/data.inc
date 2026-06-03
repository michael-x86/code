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
;   index = eax at int 0x80
;   dispatcher: see kernel/syscall.inc
;   bounds check: 0 <= eax < SYSCALL_COUNT
syscall_table:
    dd sys_putchar         ; 0  : ebx = char
    dd sys_print           ; 1  : esi = string
    dd sys_print_cr        ; 2  : esi = string (CR-aware)
    dd sys_newline         ; 3  :
    dd sys_cls             ; 4  :
    dd sys_print_hex       ; 5  : ebx = value
    dd sys_print_int       ; 6  : ebx = value
    dd sys_get_key         ; 7  : -> eax = ASCII (0 if empty)
    dd sys_get_tick        ; 8  : -> eax = tick_count
    dd sys_shutdown        ; 9  : does not return
    dd sys_read_mem        ; 10 : ebx = addr -> eax = dword at [ebx]
    dd sys_getcwd          ; 11 : edi = dst buffer
    dd sys_chdir           ; 12 : esi = path -> eax = 0 / -1
    dd sys_list_dir        ; 13 : ebx = index, edi = dst -> type / -1
    dd sys_get_arg         ; 14 : ebx = index, edi = dst -> 0 / -1
    dd sys_stat            ; 15 : esi = path, edi = info (12 B) -> 0 / -1
    dd sys_print_n         ; 16 : esi = ptr, ecx = count
    dd sys_create          ; 17 : esi = path -> 0 / -1
    dd sys_write           ; 18 : esi = path, ebx = buf, ecx = n -> 0 / -1
    dd sys_unlink          ; 19 : esi = path -> 0 / -1
    dd sys_mkdir           ; 20 : esi = path -> 0 / -1
    dd sys_rmdir           ; 21 : esi = path -> 0 / -1
    dd sys_get_ps_info     ; 22 : ebx = 16-byte dst -> 0 / -1
    dd sys_alloc_pages     ; 23 : ecx = byte count -> eax = vaddr / -1
    dd sys_free_pages      ; 24 : ebx = vaddr -> 0 / -1
    dd sys_write_mem       ; 25 : ebx = addr, ecx = byte value -> 0
    dd sys_hex2int         ; 26 : esi = string -> eax = value
    dd sys_asc2int         ; 27 : esi = string -> eax = value
    dd sys_get_proc_info   ; 28 : ebx = 36-byte dst -> 0 / -1
    dd sys_itoa            ; 29 : eax = value, edi = dst -> edi past NUL
    dd sys_get_config      ; 30 : ebx = key (0..3), edi = dst -> 0 / -1
    dd sys_stack_dump      ; 31 :
    dd sys_alloc           ; 32 : ebx = page count -> eax = vaddr / -1
    dd sys_dealloc         ; 33 : ebx = vaddr -> 0 / -1
    dd sys_peek            ; 34 : ebx = addr -> eax = byte at [ebx]
    dd sys_poke            ; 35 : ebx = addr, ecx = byte -> 0
