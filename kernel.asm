[org 0xC0100000]

global start

bits 32

MAX_PROC equ 8        ; process-table: slots 0-2 base tasks, 3+ spawned
PROC_NAME_LEN equ 16  ; snapshot for ps
PS_REC equ 24         ; sys_ps_info record size: state+vbase+esp+name[16]

; returned by get_key
KEY_LEFT  equ 0x80
KEY_RIGHT equ 0x81
KEY_UP    equ 0x82
KEY_DOWN  equ 0x83

; - 800x400x32 graphics (QEMU) -
GFX_W      equ 800
GFX_H      equ 600
GFX_PITCH  equ GFX_W*4                 ; bytes per scanline (32bpp)
GFX_FBSIZE equ GFX_W*GFX_H*4           ; framebuffer byte size
GFX_PAGES  equ (GFX_FBSIZE+4095)/4096  ; 4KB pages to map the LFB
LFB_VIRT   equ 0xC1400000              ; virtual base for the LFB (free PDE 773)
VBE_INDEX  equ 0x01CE                  ; index port
VBE_DATA   equ 0x01CF                  ; data port

; - VGA mode 0x13 (320x200x256, linear framebuffer at 0xA0000) -
M13_FB     equ 0xC00A0000        ; framebuffer (phys 0xA0000, identity-mapped)
M13_W      equ 320
M13_H      equ 200

start:
    jmp short .init_runtime
    nop

    db "KERN"       
    dd (kern_end-start) 

; Multiboot v1 header (a.out kludge) 
align 4
.mb_header:
    dd 0x1BADB002                         ; magic
    dd 0x00010000                         ; flags: a.out kludge
    dd -(0x1BADB002 + 0x00010000)         ; checksum
    dd .mb_header - 0xC0000000            ; header_addr (physical)
    dd start     - 0xC0000000             ; load_addr  = 0x00100000
    dd 0                                  ; load_end_addr (0 = load whole file)
    dd 0                                  ; bss_end_addr  (0 = don't zero bss)
    dd start     - 0xC0000000             ; entry_addr  = 0x00100000

.init_runtime:
    cli
    mov esp,0x00090000       ; temporary physical stack

    call .get_pc
.get_pc:
    pop ebp
    sub ebp,.get_pc          ; ebp = physical_load_addr-virtual_base

    ; GDT
    lea eax,[boot_gdt+ebp]
    mov [boot_gdt_desc.base+ebp],eax   ; patch base to physical (paging is off)
    lea eax,[boot_gdt_desc+ebp]
    lgdt [eax]
    lea eax,[.reload+ebp]     ; far-return into CS=0x08 at physical .reload
    push dword 0x08
    push eax
    retf
.reload:
    mov ax,0x10     ; DATA_SEG (GDT)
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax

    ; - physical stack -
    lea esp,[stack_top+ebp] 
    push ebp                 
    pushad
   
    call page_mapping

    lea eax, [page_directory+ebp]
    mov cr3,eax

    ; Enable Paging
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax

    ; Jump to virtual memory
    mov eax,higher_half
    jmp eax

higher_half:
    ; Paging is now active!
    ; We are now in virtual memory space.
    ; (Aren't we living in a virtual reality anyway?)

    mov ebp,[esp+32]    
    lea eax,[start+ebp]
    mov [kernel_phys_start_var],eax
    lea eax,[page_bitmap+32768+ebp]
    mov [kernel_phys_end_var],eax

    call reserve_kernel_pages
    
    popad
    add esp,4             
    mov esp,stack_top
    mov ebp,esp
    jmp kernel_main

; GRUB's selectors differ. Flat 4 GB, code=0x08 / data=0x10 
align 8
boot_gdt:
    dq 0x0000000000000000        ; null
    dq 0x00CF9A000000FFFF        ; 0x08 code, ring 0, flat 4 GB
    dq 0x00CF92000000FFFF        ; 0x10 data, ring 0, flat 4 GB
boot_gdt_end:
boot_gdt_desc:
    dw boot_gdt_end-boot_gdt-1
.base:
    dd boot_gdt              

; -----------------------------------------
; page directory entries
; -----------------------------------------
page_mapping:
    lea edi,[page_directory+ebp]
    xor eax,eax
    mov ecx,1024
.clear_pd:
    mov [edi],eax
    add edi,4
    loop .clear_pd

; -----------------------------------------
; kernel_low_page_table
; physical 0x00400000 - 0x007FFFFF
; -----------------------------------------
    lea edi,[kernel_low_page_table+ebp]
    mov eax,0x00400000
    mov ecx,1024
.fill_kernel:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,0x1000
    add edi,4
    loop .fill_kernel

; ------------------------------------------
; identity map first 4MB
; ------------------------------------------
    lea edi,[identity_page_table+ebp]
    xor ebx,ebx
    mov ecx,1024
.make_identity:
    mov eax,ebx
    or eax,3
    mov [edi],eax
    add ebx,4096
    add edi,4
    loop .make_identity

; ------------------------------------------
; heap_page_table_0
; physical 0x00800000-0x00BFFFFF
; ------------------------------------------
    lea edi,[heap_page_table_0+ebp]
    mov eax,0x00800000
    mov ecx,1024
.fill_heap0:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap0

; ------------------------------------------
; heap_page_table_1
; physical 0x00C00000-0x00FFFFFF
; ------------------------------------------
    lea edi,[heap_page_table_1+ebp]
    mov eax,0x00C00000
    mov ecx,1024
.fill_heap1:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap1

; ------------------------------------------
; heap_page_table_2
; physical 0x01000000-0x013FFFFF
; ------------------------------------------
    lea edi,[heap_page_table_2+ebp]
    mov eax,0x01000000
    mov ecx,1024
.fill_heap2:
    mov ebx,eax
    or ebx,3
    mov [edi],ebx
    add eax,4096
    add edi,4
    loop .fill_heap2

; -------------------------------------------
; Map PDEs 
; -------------------------------------------
    lea edx, [page_directory+ebp]

    ; PDE[0] & PDE[768] -> identity_page_table
    lea eax, [identity_page_table + ebp]
    or eax,3
    mov [edx+(0*4)],eax
    mov [edx+(768*4)],eax

    ; PDE[1] & PDE[769] -> kernel_low_page_table
    lea eax, [kernel_low_page_table + ebp]
    or eax,3
    mov [edx+(1*4)],eax
    mov [edx+(769*4)],eax

    ; PDE[2] & PDE[770] -> heap_page_table_0
    lea eax, [heap_page_table_0 + ebp]
    or eax,3
    mov [edx+(2*4)],eax
    mov [edx+(770*4)],eax

    ; PDE[3] & PDE[771] -> heap_page_table_1
    lea eax, [heap_page_table_1 + ebp]
    or eax,3
    mov [edx+(3*4)],eax
    mov [edx+(771*4)],eax

    ; PDE[4] & PDE[772] -> heap_page_table_2
    lea eax, [heap_page_table_2 + ebp]
    or eax,3
    mov [edx+(4*4)],eax
    mov [edx+(772*4)],eax
    ret

; ----------------------------------------------------
; Saving our own soul from the page allocator's greed
; ----------------------------------------------------
reserve_kernel_pages:
    push eax
    push ebx
    
    mov eax, [kernel_phys_start_var]
    and eax, 0FFFFF000h
    mov ebx, [kernel_phys_end_var]
    add ebx, 0FFFh
    and ebx, 0FFFFF000h
.reserve_loop:
    cmp eax,ebx
    jae .done
    push eax
    call set_page_used
    pop eax
    add eax,4096
    jmp .reserve_loop
.done:
    pop ebx
    pop eax
    ret

%include "kmain.inc"
%include "kterm.inc"
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

