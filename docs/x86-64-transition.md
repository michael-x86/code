# x86-64 Transition Plan & Progress

**Goal:** Migrate the kernel from 32-bit x86 protected mode to 64-bit x86 long mode.

**Current state:** Single-address-space higher-half kernel at `0xC0100000`, flat 32-bit binaries, `int 0x80` ABI, `qemu-system-i386`, NASM `-f bin`.

**Target:** Same kernel running in long mode under `qemu-system-x86_64`, with 4-level paging, 64-bit GPRs, and 64-bit IDT. Userland may initially stay as 32-bit compatibility-mode binaries.

---

## Progress Tracking

- [ ] **Phase 1: Boot Path + Toolchain** — long mode entry, ELF64, linker script
- [ ] **Phase 2: 4-Level Paging** — PML4→PDPT→PD→PT, wider PDE/PTE format
- [ ] **Phase 3: Widen Instructions** — `pushad`→manual, `iretd`→`iretq`, `loop`→`dec; jnz`, 64-bit GPRs
- [ ] **Phase 4: HAL + Interrupts** — 16-byte IDT gates, x86_64 HAL macros, `syscall`/`sysret` option
- [ ] **Phase 5: Memory Management** — 64-bit physical/virtual addresses, widened heap
- [ ] **Phase 6: Userland** — compatibility mode or native 64-bit ABI
- [ ] **Phase 7: Build System + QEMU** — `-f elf64`, `ld`, `qemu-system-x86_64`

---

## Phase 1: Boot Path + Toolchain

### Tasks

- [ ] **1.1** Switch kernel from `nasm -f bin` to `nasm -f elf64`
- [ ] **1.2** Create `kernel/link.ld` linker script placing `.text` at `0xC0100000`
- [ ] **1.3** Rewrite `bootloader.asm` to enter long mode:
  - Enable PAE (CR4.PAE = 1)
  - Set EFER.LME = 1 (MSR `0xC0000080`)
  - Build PML4 → PDP → PD identity mapping covering 0–4 MB
  - Load PML4 physical address into CR3
  - Enable paging (CR0.PG = 1)
  - Far jump with 64-bit CS selector
- [ ] **1.4** Add 64-bit code segment descriptor to GDT (`0x00209A0000000000`)
- [ ] **1.5** Change kernel.asm from `[org ...]` + `bits 32` to ELF + `bits 64`
- [ ] **1.6** Update `build/asm` for two-stage build (bootloader `-f bin`, kernel ELF64 + `ld`)

### Dependencies

None — must be done first.

### Verification

QEMU should reach long mode without triple-faulting. Add a `mov rax, 0xDEAD; jmp $` at kernel entry and verify in QEMU monitor.

---

## Phase 2: 4-Level Paging

### Tasks

- [ ] **2.1** Rewrite `paging.inc`: replace 2-level (1024 PDE × 1024 PTE) with 4-level (PML4[512] → PDPT[512] → PD[512] → PT[512])
- [ ] **2.2** Add PML4, PDPT, PD page table BSS reservations in `bss.inc` (4 KB each, 4 KB-aligned)
- [ ] **2.3** Update page table entry format: 8 bytes per entry, new flag layout (NX bit 63, accessible bit 63-52)
- [ ] **2.4** Fix `map_page` in `paging.inc`: PDE index shift changes from `shr 22` to `shr 39` (PML4), `shr 30` (PDPT), `shr 21` (PD), `shr 12` (PT offset)
- [ ] **2.5** Fix `get_pte` in `memory.inc` to walk 4 levels instead of 2
- [ ] **2.6** Update `page_mapping` to fill all 4 table levels
- [ ] **2.7** Decide higher-half layout:
  - Option A: keep `0xC0100000` (non-canonical, simpler, no user/kernel split)
  - Option B: move to `0xFFFFFF8000000000+` (canonical, enables future user/kernel split)

### Dependencies

Phase 1 (long mode must be enabled to test).

### Verification

Kernel boots, identity-mapped accesses work, heap allocator maps pages without faulting.

---

## Phase 3: Widen Instructions

### Tasks

- [ ] **3.1** Replace all `pushad`/`popad` with manual `push`/`pop` sequences
  - `kernel.asm` (startup + higher_half)
  - `irq.inc` (isr_default, irq0, irq1)
  - `panic.inc` (common_exception_handler)
- [ ] **3.2** Replace all `iretd` with `iretq`
  - `irq.inc` (isr_default, irq0, irq1)
  - `syscall.inc` (syscall_isr, .bad)
  - `kernel.asm` (kernel_main tail call)
- [ ] **3.3** Replace all `loop` with `dec rNN; jnz` (search: `\bloop\b`)
  - `paging.inc` (clear_pd, fill loops)
  - `memory.inc` (scan loops)
  - `idt.inc` (build_idt loop)
  - `task.inc` (clear loop)
  - `exec.inc` (map_loop)
  - `drivers/ata.drv`
  - `drivers/vga.drv`
  - `drivers/graphics.drv`
  - `lib.inc`
  - `panic.inc` (stack_loop)
- [ ] **3.4** Replace `push dword` with `push qword` in `exc.inc` stubs
- [ ] **3.5** Add `default rel` to kernel.asm (RIP-relative addressing)
- [ ] **3.6** Audit `add rNN, 4` → `add rNN, 8` for pointer-sized walks:
  - Page table indexing
  - Stack frame offsets
  - alloc_table walks
- [ ] **3.7** Replace `[esp + X]` with `[rsp + X]` and adjust offsets for 8-byte push frame
  - `panic.inc` (stack frame reads)
  - `syscall.inc` (potentially)
- [ ] **3.8** Widen `[org ...]` — ELF removes this; linker script handles placement

### Dependencies

Phase 2 (paging must work to reach higher-half code).

### Verification

Kernel boots, exception handler dumps 64-bit registers, IRQ0 fires, syscall dispatches.

---

## Phase 4: HAL + Interrupts

### Tasks

- [ ] **4.1** Rewrite `build_idt` in `idt.inc` for 16-byte gate descriptors:
  - Offset split into 16:16:32 bits (low, middle, high)
  - IST field (bits 0..2 of byte 4)
  - Gate type byte at byte 5 (unchanged semantics)
- [ ] **4.2** Resize `idt_start`/`idt_end` in `bss.inc`: `resb IDT_ENTRIES * 16`
- [ ] **4.3** Add x86_64 HAL CPU macros:
  - `HAL_CPU_READ_CR 8` → `mov rdx, cr8`
  - `HAL_CPU_WRITE_CR 8` → `mov cr8, rdx`
  - EFER read/write via `rdmsr`/`wrmsr`
- [ ] **4.4** Add `HAL_CPU_READ_MSR` / `HAL_CPU_WRITE_MSR` macros to `arch_cpu.inc`
- [ ] **4.5** Decide syscall path:
  - Option A: keep `int 0x80` (works in compatibility mode, simplest)
  - Option B: add `syscall`/`sysret` via MSR `LSTAR` + `STAR` (faster, 64-bit native)
- [ ] **4.6** Verify PIC, PIT, ATA work unchanged (all use port I/O)

### Dependencies

Phase 3 (IDT format ×64 change).

### Verification

PIC remap succeeds, IRQ0 fires at 100 Hz, keyboard input works, ATA reads work.

---

## Phase 5: Memory Management

### Tasks

- [ ] **5.1** Widen `page_bitmap` indexing to handle 64-bit physical addresses
  - `set_page_used`/`set_page_free`: `shr eax, 12` → `shr rax, 12`
- [ ] **5.2** `alloc_page`: return 64-bit physical frame address in RAX
- [ ] **5.3** `free_pages`: use 64-bit physical address from PTE
- [ ] **5.4** Rewrite `get_pte` to walk PML4 → PDPT → PD → PT
- [ ] **5.5** `find_free_virt`: widen alloc_table entries to 64-bit virtual addresses
- [ ] **5.6** Add E820h or ACPI memory map parsing for dynamic RAM detection (optional)

### Dependencies

Phase 2 (4-level page tables) and Phase 3 (64-bit registers).

### Verification

`alloc`/`dealloc` commands work, `free` reports correct counts, no page faults.

---

## Phase 6: Userland

### Tasks

- [ ] **6.1** Decide userland strategy:
  - Option A: **32-bit compatibility mode** — CS has `L=0, D=1`, 32-bit `int 0x80` works
  - Option B: **64-bit native** — new ABI with `syscall`, 64-bit GPRs, new binary format
- [ ] **6.2** If Option A: ensure exec.inc loads binaries into 32-bit-compatible CS
- [ ] **6.3** If Option B: rewrite `userland.inc` for 64-bit, update all 31 commands
- [ ] **6.4** Update syscall dispatcher for chosen ABI

### Recommendation

Start with Option A (compatibility mode) — zero changes to 31 existing userland programs. Add 64-bit native as a separate future milestone.

### Dependencies

Phase 4 (IDT + syscall working).

### Verification

All 31 `/bin/` commands run without modification; `ps`, `vi`, `calc`, `gdemo` all work.

---

## Phase 7: Build System + QEMU

### Tasks

- [ ] **7.1** Switch `build/asm` from `nasm -f bin` to `nasm -f elf64` for kernel.asm
- [ ] **7.2** Add `ld -m elf_x86_64 -T kernel/link.ld -o kernel.bin kernel.o`
- [ ] **7.3** Update KERNEL_SECTORS calculation in `build/asm` for ELF binary
- [ ] **7.4** Change QEMU target: `qemu-system-x86_64` instead of `qemu-system-i386`
- [ ] **7.5** Clean up `sed`-based bootloader patching if ELF layout differs
- [ ] **7.6** Add `-no-reboot` flag to QEMU for easier debugging of triple faults
- [ ] **7.7** Optionally add `-M q35` for modern chipset

### Dependencies

Phase 1 (must have bootable 64-bit kernel to test).

### Verification

`./asm -r` boots and reaches the shell prompt.

---

## Summary Stats

| Metric | Current (32-bit) | Target (64-bit) |
|--------|------------------|------------------|
| Kernel assembler | `-f bin` | `-f elf64` + `ld` |
| QEMU target | `qemu-system-i386` | `qemu-system-x86_64` |
| Paging levels | 2 (PDE→PTE) | 4 (PML4→PDPT→PD→PT) |
| Page table entry size | 4 bytes | 8 bytes |
| IDT entry size | 8 bytes | 16 bytes |
| GPR width | 32 bits | 64 bits |
| Stack frame alignment | 4 bytes | 8 bytes |
| Physical address width | 32 bits | up to 52 bits |
| `pushad`/`popad` | used | removed (manual push/pop) |
| `iretd` | used | `iretq` |
| `loop` | used | replaced with `dec; jnz` |
| Userland | flat 32-bit flat (31 programs) | compat mode (no changes) |

---

## Design Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| — | Higher-half at `0xC0100000` (Option A, Phase 2.7) | Keeps all absolute addresses ≤32 bits; minimal code churn; canonical address move can be a later optimization |
| — | Keep `int 0x80` for syscalls (Phase 4.5 Option A) | `int 0x80` works in compatibility mode; all 31 userland programs unchanged |
| — | Keep userland in 32-bit compatibility mode (Phase 6 Option A) | Zero changes to 31 existing binaries; 64-bit native ABI is a separate milestone |
