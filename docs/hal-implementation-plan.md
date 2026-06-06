# HAL (Hardware Abstraction Layer) Implementation Plan
## Architecture-Agnostic Design

## Goal
Create a **truly architecture-agnostic HAL** where hardware-specific code is isolated in `arch/` directories, and kernel code only calls abstract HAL interfaces. This enables clean ports to ARM, RISC-V, or any future architecture.

---

## Design Philosophy

### Layered Architecture
```
┌─────────────────────────────────────────┐
│   Kernel Code (syscall.inc, vfs.inc)   │  ← ONLY calls HAL_* interfaces
├─────────────────────────────────────────┤
│   HAL Layer (hal/)                      │  ← Architecture-agnostic interfaces
│   - hal_io.inc (HAL_READ_PORT, etc.)   │
│   - hal_cpu.inc (HAL_DISABLE_INTS, etc.)│
│   - hal_mem.inc (HAL_ENABLE_MMU, etc.) │
├─────────────────────────────────────────┤
│   Architecture Layer (arch/x86/)        │  ← x86-specific implementations
│   - arch/x86/io.inc (uses 'in'/'out') │
│   - arch/x86/cpu.inc (uses 'cli'/'sti')│
│   - arch/x86/paging.inc (CR3, invlpg) │
├─────────────────────────────────────────┤
│   Hardware (PIC, PIT, MMU, etc.)      │
└─────────────────────────────────────────┘
```

### Key Principles
1. **HAL defines interfaces**, arch/ provides implementations
2. **Kernel code never uses `in`/`out`/`cli`/`sti` directly** — only HAL macros
3. **Same HAL interface on all architectures** — different implementations underneath
4. **Arch-specific code stays in `arch/`** — never leaks into hal/ or kernel/

---

## Current Hardware-Specific Code Identified

### High Priority (Must be abstracted)
1. **pic.inc** — 8259A PIC (`out` to PIC ports) → arch/x86/
2. **paging.inc** — Page tables, CR3, `invlpg` → arch/x86/
3. **idt.inc** — IDT gate descriptors (x86-specific structure) → arch/x86/
4. **syscall.inc** — Control characters (belongs in HAL, not arch)

### Medium Priority
5. **Port I/O** — Scattered `in`/`out` instructions → arch/x86/io.inc
6. **CPU control** — `cli`/`sti`, `lgdt`, `lidt` → arch/x86/cpu.inc
7. **PIT/timer** — Channel 0 programming → arch/x86/timer.inc

### Lower Priority (Already partially separated)
8. **VGA operations** — vga.inc (keep as-is, already separated)
9. **Keyboard controller** — i8042 operations (if present)

---

## Directory Structure (After HAL)

```
kernel/src/
├── includes/
│   ├── hal.inc              ; Main HAL header (includes all hal/*.inc)
│   ├── hal/                 ; Architecture-AGNOSTIC interfaces
│   │   ├── hal_io.inc       ; Port I/O interface (HAL_READ_PORT, etc.)
│   │   ├── hal_cpu.inc      ; CPU control (HAL_DISABLE_INTS, etc.)
│   │   ├── hal_intr.inc     ; Interrupt controller (HAL_INTR_INIT, etc.)
│   │   ├── hal_mem.inc      ; Memory management (HAL_ENABLE_MMU, etc.)
│   │   ├── hal_timer.inc    ; Timer (HAL_TIMER_INIT, etc.)
│   │   └── hal_debug.inc    ; Debug output (HAL_PUTCHAR, etc.)
│   ├── arch/                ; Architecture-SPECIFIC implementations
│   │   └── x86/            ; x86 implementation
│   │       ├── arch_io.inc       ; Implements hal_io using 'in'/'out'
│   │       ├── arch_cpu.inc      ; Implements hal_cpu using 'cli'/'sti'
│   │       ├── arch_intr.inc     ; Implements hal_intr for 8259A PIC
│   │       ├── arch_mem.inc      ; Implements hal_mem for x86 paging
│   │       ├── arch_timer.inc    ; Implements hal_timer for PIT
│   │       └── arch_debug.inc    ; Implements hal_debug for VGA/serial
│   ├── syscall.inc          ; Syscall dispatcher (uses HAL interfaces)
│   ├── lib.inc              ; Standard library
│   └── ...                 ; Other includes
├── commands/                ; Userland programs (use HAL via syscall only)
└── kernel.asm              ; Kernel entry (uses HAL + arch/)
```

**For future ARM port:**
```
│   ├── arch/
│   │   ├── x86/            ; x86 implementation (existing)
│   │   └── arm/            ; ARM implementation (future)
│   │       ├── arch_io.inc       ; Memory-mapped I/O or co-processor
│   │       ├── arch_cpu.inc      ; cpsid/cpsie instructions
│   │       ├── arch_intr.inc     ; GIC (Generic Interrupt Controller)
│   │       ├── arch_mem.inc      ; ARM MMU (TTBR0, TTBCR, etc.)
│   │       ├── arch_timer.inc    ; ARM Generic Timer
│   │       └── arch_debug.inc    ; UART or framebuffer
```

---

## Incremental Implementation Plan

### Phase 1: HAL Infrastructure + IO Abstraction
**Goal**: Create directory structure and abstract port I/O

#### 1.1 Create Directory Structure
```bash
mkdir -p kernel/src/includes/hal
mkdir -p kernel/src/includes/arch/x86
```

#### 1.2 Define HAL IO Interface (`hal/hal_io.inc`)
```nasm
; =============================================================================
; hal_io.inc — Architecture-agnostic I/O interface
; =============================================================================
; These macros define the interface. Architecture-specific code in arch/x86/
; provides the actual implementation.

; Read from I/O port
; Usage: HAL_READ_PORT port, reg
;        port = immediate or register (al, ax, eax)
;        reg  = register to store result (al, ax, eax)
%macro HAL_READ_PORT 2
    %ifnidn %1, al
    %ifnidn %1, ax
    %ifnidn %1, eax
        mov dx, %1              ; port in dx (can be immediate or register)
    %endif
    %endif
    %ifidn %2, al
        in al, dx
    %elifidn %2, ax
        in ax, dx
    %elifidn %2, eax
        in eax, dx
    %else
        %error "HAL_READ_PORT: reg must be al, ax, or eax"
    %endif
%endmacro

; Write to I/O port
; Usage: HAL_WRITE_PORT port, reg
%macro HAL_WRITE_PORT 2
    %ifnidn %1, al
    %ifnidn %1, ax
    %ifnidn %1, eax
        mov dx, %1
    %endif
    %endif
    %ifidn %2, al
        out dx, al
    %elifidn %2, ax
        out dx, ax
    %elifidn %2, eax
        out dx, eax
    %else
        %error "HAL_WRITE_PORT: reg must be al, ax, or eax"
    %endif
%endmacro

; Memory-mapped I/O read (for architectures without port I/O)
; Usage: HAL_MMIO_READ addr, reg
%macro HAL_MMIO_READ 2
    mov %2, [%1]
%endmacro

; Memory-mapped I/O write
; Usage: HAL_MMIO_WRITE addr, reg
%macro HAL_MMIO_WRITE 2
    mov [%1], %2
%endmacro
```

**Wait — this still uses `in`/`out`! That's x86-specific.**

Let me redesign this properly. The HAL should define *what* we want to do (read UART, write PIC), and arch/ decides *how* (port I/O vs memory-mapped).

#### 1.2 REVISED: Define HAL IO Interface (`hal/hal_io.inc`)
```nasm
; =============================================================================
; hal_io.inc — Architecture-agnostic I/O interface
; =============================================================================
; KERNEL CODE calls these macros. They expand to architecture-specific
; implementations via %include from arch/<arch>/arch_io.inc

; --- Device-specific read/write (preferred) ---
; These are what kernel code should use. The arch layer maps them to
; the correct ports/addresses.

; PIC (Programmable Interrupt Controller)
%macro HAL_PIC_READ_CMD  1    ; HAL_PIC_READ_CMD  master/slave → reg
%macro HAL_PIC_READ_DATA 1    ; HAL_PIC_READ_DATA master/slave → reg
%macro HAL_PIC_WRITE_CMD 1    ; HAL_PIC_WRITE_CMD master/slave ← reg
%macro HAL_PIC_WRITE_DATA 1   ; HAL_PIC_WRITE_DATA master/slave ← reg

; PIT (Programmable Interval Timer)
%macro HAL_PIT_READ_CMD  0
%macro HAL_PIT_WRITE_CMD 1    ; HAL_PIT_WRITE_CMD reg
%macro HAL_PIT_WRITE_DATA 1   ; HAL_PIT_WRITE_DATA channel, reg

; UART (serial port)
%macro HAL_UART_READ 1        ; HAL_UART_READ port → reg
%macro HAL_UART_WRITE 1       ; HAL_UART_WRITE port ← reg

; Keyboard controller (i8042)
%macro HAL_KBD_READ_DATA 0
%macro HAL_KBD_READ_STATUS 0
%macro HAL_KBD_WRITE_DATA 1   ; HAL_KBD_WRITE_DATA reg
%macro HAL_KBD_WRITE_CMD 1    ; HAL_KBD_WRITE_CMD reg

; --- Generic I/O (use only if device-specific macros don't exist) ---
%macro HAL_IO_READ  2         ; HAL_IO_READ port, reg
%macro HAL_IO_WRITE 2         ; HAL_IO_WRITE port, reg
%macro HAL_MMIO_READ 2        ; HAL_MMIO_READ addr, reg (for ARM, etc.)
%macro HAL_MMIO_WRITE 2       ; HAL_MMIO_WRITE addr, reg
```

#### 1.3 Implement x86 IO (`arch/x86/arch_io.inc`)
```nasm
; =============================================================================
; arch/x86/arch_io.inc — x86 I/O implementation
; =============================================================================
; This file implements the HAL IO interface using x86 'in'/'out' instructions.
; For memory-mapped architectures (ARM), this file would use load/store instead.

; --- PIC (8259A) ---
%define PIC_MASTER_CMD   0x20
%define PIC_MASTER_DATA  0x21
%define PIC_SLAVE_CMD    0xA0
%define PIC_SLAVE_DATA   0xA1

%macro HAL_PIC_READ_CMD 1
    %ifidn %1, master
        in al, PIC_MASTER_CMD
    %elifidn %1, slave
        in al, PIC_SLAVE_CMD
    %else
        %error "HAL_PIC_READ_CMD: must be 'master' or 'slave'"
    %endif
%endmacro

%macro HAL_PIC_WRITE_CMD 1
    %ifidn %1, master
        out PIC_MASTER_CMD, al
    %elifidn %1, slave
        out PIC_SLAVE_CMD, al
    %endif
%endmacro

; ... (similar for DATA, PIT, UART, etc.)

; --- Generic I/O ---
%macro HAL_IO_READ 2
    mov dx, %1
    in al, dx
    mov %2, al
%endmacro

%macro HAL_IO_WRITE 2
    mov dx, %1
    mov al, %2
    out dx, al
%endmacro
```

#### 1.4 Migration Step
1. Create `hal/hal_io.inc` with interface macros (empty, to be filled by arch)
2. Create `arch/x86/arch_io.inc` with x86 implementation
3. Update `pic.inc` to use `HAL_PIC_WRITE_CMD master` instead of `out PIC_MASTER_CMD, al`
4. Test compilation and boot

**Verification**: `./asm` compiles, QEMU boots, keyboard/timer work

---

### Phase 2: CPU Control Abstraction
**Goal**: Abstract interrupt control, GDT/IDT loading, etc.

#### 2.1 Define HAL CPU Interface (`hal/hal_cpu.inc`)
```nasm
; =============================================================================
; hal_cpu.inc — Architecture-agnostic CPU control interface
; =============================================================================

; --- Interrupt control ---
%macro HAL_DISABLE_INTS 0     ; Disable interrupts (cli on x86, cpsid on ARM)
%macro HAL_ENABLE_INTS 0      ; Enable interrupts (sti on x86, cpsie on ARM)
%macro HAL_SAVE_INT_STATE 0   ; Push EFLAGS/PRIMASK to stack
%macro HAL_RESTORE_INT_STATE 0; Pop EFLAGS/PRIMASK from stack

; --- Descriptor table loads ---
%macro HAL_LOAD_GDT 1         ; Load GDT from address (lgdt on x86)
%macro HAL_LOAD_IDT 1         ; Load IDT from address (lidt on x86)
%macro HAL_LOAD_TSS 1         ; Load TSS selector (ltr on x86)

; --- Control registers ---
%macro HAL_READ_CR 2          ; HAL_READ_CR cr_num, reg (x86 CR0-CR4)
%macro HAL_WRITE_CR 2         ; HAL_WRITE_CR cr_num, reg
; Note: ARM doesn't have control registers; use coprocessor instructions

; --- Cache/tlb control ---
%macro HAL_INVLPG 1           ; Invalidate TLB entry (x86 invlpg)
%macro HAL_FLUSH_CACHE 0      ; Flush CPU cache
%macro HAL_FLUSH_TLB 0        ; Flush entire TLB (write CR3 on x86)

; --- Halt/wait ---
%macro HAL_HALT 0             ; Halt CPU (hlt on x86, wfi on ARM)
%macro HAL_WAIT_FOR_INTR 0    ; Wait for interrupt (same as HAL_HALT on x86)
```

#### 2.2 Implement x86 CPU (`arch/x86/arch_cpu.inc`)
```nasm
; =============================================================================
; arch/x86/arch_cpu.inc — x86 CPU control implementation
; =============================================================================

%macro HAL_DISABLE_INTS 0
    cli
%endmacro

%macro HAL_ENABLE_INTS 0
    sti
%endmacro

%macro HAL_SAVE_INT_STATE 0
    pushfd
    cli
%endmacro

%macro HAL_RESTORE_INT_STATE 0
    popfd                      ; Restores interrupt flag
%endmacro

%macro HAL_LOAD_GDT 1
    lgdt [%1]
%endmacro

%macro HAL_LOAD_IDT 1
    lidt [%1]
%endmacro

%macro HAL_LOAD_TSS 1
    ltr %1
%endmacro

%macro HAL_READ_CR 2
    %if %1 == 0
        mov %2, cr0
    %elif %1 == 2
        mov %2, cr2
    %elif %1 == 3
        mov %2, cr3
    %elif %1 == 4
        mov %2, cr4
    %endif
%endmacro

%macro HAL_WRITE_CR 2
    %if %1 == 0
        mov cr0, %2
    %elif %1 == 3
        mov cr3, %2
    %elif %1 == 4
        mov cr4, %2
    %endif
%endmacro

%macro HAL_INVLPG 1
    invlpg [%1]
%endmacro

%macro HAL_FLUSH_TLB 0
    mov eax, cr3
    mov cr3, eax
%endmacro

%macro HAL_HALT 0
    hlt
%endmacro

%macro HAL_WAIT_FOR_INTR 0
    hlt                        ; x86: hlt waits for interrupt
%endmacro
```

#### 2.3 Migration Step
1. Replace `cli`/`sti` in kernel code with `HAL_DISABLE_INTS`/`HAL_ENABLE_INTS`
2. Replace `lgdt`/`lidt` with `HAL_LOAD_GDT`/`HAL_LOAD_IDT`
3. **IMPORTANT**: Do NOT replace cli/sti in userland code (ring 3) — it will #GP fault
   - Userland should use syscalls for interaction with hardware
   - If userland code has cli/sti, it's a bug — leave it as-is

**Verification**: Run scheduler, ensure timer ticks and context switches work

---

### Phase 3: Interrupt Controller Abstraction
**Goal**: Abstract PIC/APIC so we can swap 8259A for APIC or GIC (ARM)

#### 3.1 Define HAL Interrupt Interface (`hal/hal_intr.inc`)
```nasm
; =============================================================================
; hal_intr.inc — Architecture-agnostic interrupt controller interface
; =============================================================================

; --- Initialization ---
%macro HAL_INTR_INIT 0         ; Initialize interrupt controller
%macro HAL_INTR_SECONDARY_INIT 0 ; Initialize secondary controller (APIC, etc.)

; --- IRQ control ---
%macro HAL_INTR_MASK 1         ; Mask IRQ number (0-15)
%macro HAL_INTR_UNMASK 1       ; Unmask IRQ number (0-15)
%macro HAL_INTR_MASK_ALL 0     ; Mask all IRQs
%macro HAL_INTR_UNMASK_ALL 0   ; Unmask all IRQs (dangerous!)

; --- EOI (End of Interrupt) ---
%macro HAL_INTR_EOI 1         ; Send EOI for IRQ number (0-15)
%macro HAL_INTR_EOI_MASTER 0  ; Send EOI to master only (x86 PIC-specific)
%macro HAL_INTR_EOI_SLAVE 0   ; Send EOI to slave only (x86 PIC-specific)

; --- Spurious interrupt handling ---
%macro HAL_INTR_READ_ISR 0    ; Read In-Service Register
%macro HAL_INTR_READ_IRR 0    ; Read Interrupt Request Register
%macro HAL_INTR_IS_SPURIOUS 1 ; Check if IRQ is spurious (returns ZF=1 if spurious)

; --- IRQ-to-vector mapping ---
%define HAL_INTR_BASE_VECTOR 0x20  ; First IRQ vector (configurable)
%define HAL_INTR_IRQ_COUNT   16     ; Number of IRQs (x86 = 16, ARM GIC = 160+)
```

#### 3.2 Implement x86 PIC (`arch/x86/arch_intr.inc`)
```nasm
; =============================================================================
; arch/x86/arch_intr.inc — x86 8259A PIC implementation
; =============================================================================

%define PIC_MASTER_OFFSET  HAL_INTR_BASE_VECTOR
%define PIC_SLAVE_OFFSET   (HAL_INTR_BASE_VECTOR + 8)

%macro HAL_INTR_INIT 0
    ; Remap PIC (current pic_remap code)
    HAL_PIC_WRITE_CMD master, PIC_INIT
    HAL_PIC_WRITE_CMD slave, PIC_INIT
    HAL_PIC_WRITE_DATA master, PIC_MASTER_OFFSET
    HAL_PIC_WRITE_DATA slave, PIC_SLAVE_OFFSET
    ; ... (rest of pic_remap code using HAL_PIC_* macros)
%endmacro

%macro HAL_INTR_MASK 1
    ; Read IMR, set bit for IRQ %1, write back
    %if %1 < 8
        HAL_PIC_READ_DATA master
        or al, (1 << %1)
        HAL_PIC_WRITE_DATA master, al
    %else
        HAL_PIC_READ_DATA slave
        or al, (1 << (%1 - 8))
        HAL_PIC_WRITE_DATA slave, al
    %endif
%endmacro

%macro HAL_INTR_UNMASK 1
    ; Read IMR, clear bit for IRQ %1, write back
    %if %1 < 8
        HAL_PIC_READ_DATA master
        and al, ~(1 << %1)
        HAL_PIC_WRITE_DATA master, al
    %else
        HAL_PIC_READ_DATA slave
        and al, ~(1 << (%1 - 8))
        HAL_PIC_WRITE_DATA slave, al
    %endif
%endmacro

%macro HAL_INTR_EOI 1
    %if %1 >= 8
        HAL_PIC_WRITE_CMD slave, PIC_EOI
    %endif
    HAL_PIC_WRITE_CMD master, PIC_EOI
%endmacro
```

#### 3.3 Migration Step
1. Move current `pic_remap` code to `arch/x86/arch_intr.inc`
2. Replace all `out PIC_MASTER_CMD, al` with `HAL_PIC_WRITE_CMD master`
3. Update IRQ handlers to use `HAL_INTR_EOI irq_num`
4. Remove old `pic.inc`

**Verification**: Timer (IRQ0) and keyboard (IRQ1) still work

---

### Phase 4: Memory Management Abstraction
**Goal**: Abstract paging/MMU so we can support x86 paging or ARM MMU

#### 4.1 Define HAL Memory Interface (`hal/hal_mem.inc`)
```nasm
; =============================================================================
; hal_mem.inc — Architecture-agnostic memory management interface
; =============================================================================

; --- MMU control ---
%macro HAL_MMU_ENABLE 0        ; Enable MMU/paging
%macro HAL_MMU_DISABLE 0      ; Disable MMU/paging
%macro HAL_MMU_IS_ENABLED 0   ; Check if MMU is enabled (sets ZF)

; --- Page table operations ---
%macro HAL_MMU_MAP 3          ; HAL_MMU_MAP virt, phys, flags
%macro HAL_MMU_UNMAP 1       ; HAL_MMU_UNMAP virt
%macro HAL_MMU_MAP_RANGE 4   ; HAL_MMU_MAP_RANGE virt, phys, count, flags
%macro HAL_MMU_GET_PHYS 1    ; HAL_MMU_GET_PHYS virt → eax = physical

; --- TLB control ---
%macro HAL_MMU_INVLPG 1      ; Invalidate TLB entry
%macro HAL_MMU_FLUSH_TLB 0   ; Flush entire TLB

; --- Flags (architecture-independent) ---
%define HAL_MEM_READ          0x01
%define HAL_MEM_WRITE         0x02
%define HAL_MEM_EXEC         0x04
%define HAL_MEM_USER         0x08
%define HAL_MEM_PRESENT      0x10
%define HAL_MEM_CACHED       0x20
%define HAL_MEM_BUFFERED     0x40

; --- Architecture-specific flag conversion ---
; Each arch/ converts HAL_MEM_* flags to arch-specific flags (PAGE_PRESENT_RW on x86)
%macro HAL_MEM_FLAGS_TO_ARCH 1  ; Convert HAL flags in %1 to arch flags in eax
```

#### 4.2 Implement x86 Paging (`arch/x86/arch_mem.inc`)
```nasm
; =============================================================================
; arch/x86/arch_mem.inc — x86 paging implementation
; =============================================================================

; Convert HAL_MEM_* flags to x86 page table flags
%macro HAL_MEM_FLAGS_TO_ARCH 1
    xor eax, eax
    test %1, HAL_MEM_PRESENT
    jz .not_present
    or eax, 0x01              ; Present bit (PAGE_PRESENT)
.not_present:
    test %1, HAL_MEM_WRITE
    jz .not_write
    or eax, 0x02              ; Read/write (PAGE_RW)
.not_write:
    test %1, HAL_MEM_USER
    jz .not_user
    or eax, 0x04              ; User/supervisor (PAGE_USER)
.not_user:
    ; ... (continue for other flags)
%endmacro

%macro HAL_MMU_ENABLE 0
    HAL_READ_CR cr0, eax
    or eax, 0x80000000        ; Set PG bit (bit 31)
    HAL_WRITE_CR cr0, eax
%endmacro

%macro HAL_MMU_DISABLE 0
    HAL_READ_CR cr0, eax
    and eax, 0x7FFFFFFF       ; Clear PG bit
    HAL_WRITE_CR cr0, eax
%endmacro

%macro HAL_MMU_MAP 3
    ; %1 = virtual address, %2 = physical address, %3 = flags
    ; Walk page directory, allocate page table if needed, set PTE
    ; ... (current map_page code, refactored)
%endmacro
```

#### 4.3 Migration Step
1. Move `page_mapping` and `map_page` to `arch/x86/arch_mem.inc`
2. Replace `mov cr3, eax` with `HAL_WRITE_CR 3, eax`
3. Replace `invlpg [addr]` with `HAL_MMU_INVLPG addr`
4. Update `exec.inc` to use `HAL_MMU_MAP` instead of direct page table manipulation

**Verification**: Test fork/exec, heap allocation, ensure no page faults

---

### Phase 5: Timer Abstraction
**Goal**: Abstract timer so we can use PIT, APIC, or ARM Generic Timer

#### 5.1 Define HAL Timer Interface (`hal/hal_timer.inc`)
```nasm
; =============================================================================
; hal_timer.inc — Architecture-agnostic timer interface
; =============================================================================

; --- Initialization ---
%macro HAL_TIMER_INIT 1       ; HAL_TIMER_INIT frequency_hz
%macro HAL_TIMER_DEINIT 0     ; Disable timer

; --- Control ---
%macro HAL_TIMER_ENABLE 0     ; Enable timer interrupt
%macro HAL_TIMER_DISABLE 0    ; Disable timer interrupt
%macro HAL_TIMER_SET_FREQ 1   ; HAL_TIMER_SET_FREQ hz

; --- Read ---
%macro HAL_TIMER_GET_TICKS 0 ; Get current tick count → eax
%macro HAL_TIMER_GET_UPTIME 0; Get uptime in ms → eax
%macro HAL_TIMER_GET_COUNTER 0; Get raw counter value → eax

; --- Callback ---
%macro HAL_TIMER_SET_HANDLER 1; HAL_TIMER_SET_HANDLER label (ISR)

; --- One-shot / periodic ---
%macro HAL_TIMER_ONESHOT 1   ; HAL_TIMER_ONESHOT microseconds
%macro HAL_TIMER_PERIODIC 1  ; HAL_TIMER_PERIODIC microseconds
```

#### 5.2 Implement x86 PIT (`arch/x86/arch_timer.inc`)
```nasm
; =============================================================================
; arch/x86/arch_timer.inc — x86 PIT implementation
; =============================================================================

%define PIT_CMD_PORT   0x43
%define PIT_CH0_PORT   0x40
%define PIT_BASE_FREQ  1193182

%macro HAL_TIMER_INIT 1
    ; Calculate divisor: PIT_BASE_FREQ / %1
    mov eax, PIT_BASE_FREQ
    xor edx, edx
    mov ecx, %1
    div ecx                   ; eax = divisor
    ; Program PIT channel 0 in square-wave mode
    HAL_IO_WRITE PIT_CMD_PORT, 0x36
    HAL_IO_WRITE PIT_CH0_PORT, al   ; Low byte
    HAL_IO_WRITE PIT_CH0_PORT, ah   ; High byte
    ; Enable IRQ0
    HAL_INTR_UNMASK 0
%endmacro

%macro HAL_TIMER_GET_TICKS 0
    mov eax, [timer_ticks]    ; Defined in kernel data section
%endmacro
```

#### 5.3 Migration Step
1. Move PIT code from `pic.inc` to `arch/x86/arch_timer.inc`
2. Update scheduler to use `HAL_TIMER_GET_TICKS` instead of reading `timer_ticks` directly
3. Remove old PIT code from `pic.inc`

**Verification**: Scheduler still preempts at correct frequency

---

### Phase 6: Debug Output Abstraction
**Goal**: Abstract console output so we can use VGA, serial, or framebuffer

#### 6.1 Define HAL Debug Interface (`hal/hal_debug.inc`)
```nasm
; =============================================================================
; hal_debug.inc — Architecture-agnostic debug output interface
; =============================================================================

; --- Character output ---
%macro HAL_PUTCHAR 1         ; HAL_PUTCHAR char (register or immediate)
%macro HAL_PUTS 1            ; HAL_PUTS string_label
%macro HAL_PUTS_CR 0         ; HAL_PUTS_CR (carriage return + newline)

; --- Cursor control ---
%macro HAL_CURSOR_MOVE 2    ; HAL_CURSOR_MOVE row, col
%macro HAL_CURSOR_GET 2     ; HAL_CURSOR_GET row, col
%macro HAL_CURSOR_ENABLE 0
%macro HAL_CURSOR_DISABLE 0

; --- Screen control ---
%macro HAL_SCREEN_CLEAR 0
%macro HAL_SCREEN_SCROLL 0
%macro HAL_SCREEN_SET_MODE 1; HAL_SCREEN_SET_MODE mode (text/gfx)

; --- Color ---
%macro HAL_SET_FG_COLOR 1   ; HAL_SET_FG_COLOR color
%macro HAL_SET_BG_COLOR 1   ; HAL_SET_BG_COLOR color
%macro HAL_SET_COLOR 1      ; HAL_SET_COLOR fg_bg_combined
```

#### 6.2 Implement x86 VGA (`arch/x86/arch_debug.inc`)
```nasm
; =============================================================================
; arch/x86/arch_debug.inc — x86 VGA text mode implementation
; =============================================================================

%define VGA_WIDTH       80
%define VGA_HEIGHT      25
%define VGA_BUFFER     0xB8000

%macro HAL_PUTCHAR 1
    ; Current putchar code from vga.inc, adapted
    push eax
    push ebx
    mov al, %1
    call vga_putchar        ; Existing function, renamed to avoid conflict
    pop ebx
    pop eax
%endmacro

%macro HAL_SCREEN_CLEAR 0
    ; Clear VGA buffer
    push edi
    push eax
    mov edi, VGA_BUFFER
    mov eax, 0x07200720     ; Blank character (white on black)
    mov ecx, VGA_WIDTH * VGA_HEIGHT / 2
    rep stosd
    pop eax
    pop edi
%endmacro
```

#### 6.3 Migration Step
1. Create `HAL_PUTCHAR` macro that wraps current `putchar` logic
2. Update `syscall.inc` to use `HAL_PUTCHAR` instead of inline VGA code
3. Keep `vga.inc` as fallback for direct VGA access

**Verification**: Shell still displays correctly

---

### Phase 7: Update Kernel Entry + Build System
**Goal**: Wire up HAL and arch/ layers correctly

#### 7.1 Update `kernel.asm`
```nasm
; At top of file
%include "hal/hal.inc"        ; Includes all hal/*.inc
%include "arch/x86/arch.inc"  ; Includes all arch/x86/*.inc

[bits 32]
[extern main]

section .text
global _start
_start:
    ; --- HAL initialization ---
    HAL_DISABLE_INTS          ; cli (x86) / cpsid (ARM)
    
    ; --- Architecture-specific initialization ---
    call arch_early_init      ; Set up segments, GDT, etc.
    
    ; --- HAL initialization ---
    HAL_INTR_INIT             ; Initialize interrupt controller
    HAL_TIMER_INIT 100        ; 100 Hz timer
    HAL_MMU_ENABLE            ; Enable paging (if configured)
    
    ; --- Enable interrupts ---
    HAL_ENABLE_INTS           ; sti (x86) / cpsie (ARM)
    
    ; --- Call kernel main ---
    call main
    
    ; --- Halt if main returns ---
    HAL_HALT
```

#### 7.2 Update Build System (`asm` script or Makefile)
```bash
# In asm script, add include paths:
nasm -I kernel/src/includes/ \
     -I kernel/src/includes/hal/ \
     -I kernel/src/includes/arch/x86/ \
     kernel/src/kernel.asm -o kernel.bin
```

**Verification**: Full kernel compiles and boots

---

### Phase 8: Cleanup + Documentation
**Goal**: Remove all direct hardware access, document HAL

#### 8.1 Audit
Search for any remaining hardware-specific instructions:
```bash
grep -r "out " kernel/src/includes/*.inc
grep -r "in " kernel/src/includes/*.inc
grep -r "cli\|sti" kernel/src/includes/*.inc
grep -r "lgdt\|lidt" kernel/src/includes/*.inc
```

Any matches found = missed HAL migration, fix them.

#### 8.2 Documentation
Add to each HAL file:
```nasm
; =============================================================================
; hal_*.inc — Architecture-agnostic interface for <functionality>
; =============================================================================
; USAGE: Kernel code calls HAL_* macros defined here.
;        Architecture-specific code in arch/<arch>/ provides implementation.
;
; PORTING: To port to a new architecture:
;   1. Create arch/<new_arch>/ directory
;   2. Implement all HAL_* macros for your architecture
;   3. Update kernel.asm to %include your arch/<new_arch>/arch.inc
; =============================================================================
```

#### 8.3 Create Arch-Switching Mechanism
In `kernel.asm`:
```nasm
; Select architecture (compile-time)
%ifdef ARCH_X86
    %include "arch/x86/arch.inc"
%elifdef ARCH_ARM
    %include "arch/arm/arch.inc"
%else
    %error "No architecture selected. Define ARCH_X86 or ARCH_ARM"
%endif
```

Pass `-DARCH_X86` or `-DARCH_ARM` in build system.

---

## Migration Rules (from Memory)

1. **userland.inc rule**: Contains ONLY macros, NO function definitions
   - Functions generate code at include site → violates HAL layering
   - Use `hal_debug.inc` for debug output in userland (via syscall)

2. **Debug output rule**: No leftover debug prints in commits
   - Add debug ONLY when actively diagnosing
   - Remove ALL debug before reporting task complete
   - Use `HAL_PUTCHAR` for debug output (abstracted)

3. **Modular includes**: Create domain-specific .inc files in `arch/x86/`
   - Use `%include` for dependencies between arch files
   - Enables code reuse across architectures (if similar)

4. **Architecture rule**: 
   - `syscall.inc` handles control characters (backspace=8, tab=9, newline=10/13)
   - `vga.inc` is ONLY for VGA hardware (putchar writes to buffer, newline scrolls)
   - `hal_debug.inc` wraps these for architecture-agnostic access

5. **HAL migration rule**: 
   - userland (ring 3) MUST NOT use `cli`/`sti` (causes #GP fault)
   - Do NOT add `HAL_DISABLE_INTS`/`HAL_ENABLE_INTS` to userland.inc
   - Only kernel code (ring 0) should use these
   - Userland interacts with hardware via syscalls ONLY

6. **NEW: Architecture-agnostic rule**:
   - HAL layer (`hal/`) must NOT contain architecture-specific code
   - Arch layer (`arch/x86/`) must NOT be called directly by kernel code
   - Kernel code calls HAL, HAL expands to arch/ implementation via `%include`

---

## Testing Strategy

After each phase:
1. **Compile test**: `./asm` or `make` — ensure no assembly errors
2. **Boot test**: QEMU with `-serial stdio` — verify kernel loads
3. **Smoke test**: Type in shell, run a command, ensure basic functionality
4. **Regression test**: Run previous working programs (enigma, elite, etc.)
5. **HAL compliance test**: `grep -r "out\|in\|cli\|sti" kernel/src/includes/*.inc`
   - Should only find matches in `arch/x86/` directory
   - If found in `hal/` or top-level `includes/`, that's a bug

Keep old implementations in separate branch until HAL fully tested!

---

## Example: Before and After HAL

### Before (kernel code directly uses x86 instructions)
```nasm
; In syscall.inc (sys_putchar):
mov al, bl
out 0x3F8, al              ; Direct UART output (x86-specific)

; In irq0 handler:
mov al, 0x20
out 0x20, al               ; Direct PIC EOI (x86-specific)

; In kernel.asm:
cli                         ; Direct interrupt disable (x86-specific)
lgdt [gdt_descriptor]       ; Direct GDT load (x86-specific)
```

### After (kernel code uses HAL interfaces)
```nasm
; In syscall.inc (sys_putchar):
HAL_UART_WRITE DATA, bl    ; Abstract UART write (any architecture)

; In irq0 handler:
HAL_INTR_EOI 0             ; Abstract EOI (works for PIC, APIC, GIC, etc.)

; In kernel.asm:
HAL_DISABLE_INTS           ; Abstract interrupt disable (cli on x86, cpsid on ARM)
HAL_LOAD_GDT gdt_desc      ; Abstract GDT load (lgdt on x86, different on ARM)
```

---

## Timeline Estimate (Revised)

- Phase 1 (HAL IO + Infrastructure): 3-4 hours
- Phase 2 (CPU Control): 2-3 hours
- Phase 3 (Interrupt Controller): 3-4 hours
- Phase 4 (Memory Management): 4-5 hours
- Phase 5 (Timer): 2-3 hours
- Phase 6 (Debug Output): 2-3 hours
- Phase 7 (Kernel Entry + Build): 2-3 hours
- Phase 8 (Cleanup + Documentation): 3-4 hours

**Total**: ~25-30 hours of focused work

---

## Future Architecture Ports

With this HAL in place, porting to a new architecture requires:

1. **Create `arch/<arch>/` directory** (e.g., `arch/arm/`)
2. **Implement all `hal_*.inc` interfaces** in `arch/<arch>/arch_*.inc`
3. **Update build system** to compile for new architecture
4. **Test on actual hardware or emulator** (QEMU system-mode for ARM)

### Example: ARM Port Checklist
- [ ] `arch/arm/arch_io.inc` — Memory-mapped I/O or co-processor instructions
- [ ] `arch/arm/arch_cpu.inc` — `cpsid`/`cpsie`, `mrc`/`mcr` for co-processor
- [ ] `arch/arm/arch_intr.inc` — GIC (Generic Interrupt Controller)
- [ ] `arch/arm/arch_mem.inc` — ARM MMU (TTBR0, TTBCR, etc.)
- [ ] `arch/arm/arch_timer.inc` — ARM Generic Timer (CNTPCT, CNTFRQ)
- [ ] `arch/arm/arch_debug.inc` — UART or framebuffer

---

## Next Steps

1. **User approval**: Review this revised plan
2. **Start Phase 1**: Create HAL infrastructure + IO abstraction
3. **Incremental commits**: Commit after each phase with message "HAL Phase N: ..."
4. **Test continuously**: Never let kernel break for more than one phase
5. **Document as you go**: Add comments to HAL files explaining the interface

Shall I begin with Phase 1 (HAL Infrastructure + IO Abstraction)?
