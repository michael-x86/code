# HAL (Hardware Abstraction Layer) Implementation Plan & Progress
## Architecture-Agnostic Design

**Last Updated:** Phase 1 Complete (June 6, 2026)

---

## Progress Tracking

### Completed Phases
- [x] **Phase 1: HAL Infrastructure + I/O Abstraction** (Completed June 6, 2026)
  - Created directory structure: `hal/` and `arch/x86/`
  - Created `hal/hal.inc` - Main HAL header with architecture selection
  - Created `arch/x86/arch_io.inc` - x86 I/O implementation using `in`/`out`
  - Migrated `pic.inc` to use HAL macros
  - Updated `build/asm` with new include paths
  - **Build verified:** Kernel compiles successfully (19911 bytes)

### Pending Phases
- [ ] Phase 2: CPU Control Abstraction
- [ ] Phase 3: Interrupt Controller Abstraction
- [ ] Phase 4: Memory Management Abstraction
- [ ] Phase 5: Timer Abstraction
- [ ] Phase 6: Debug Output Abstraction
- [ ] Phase 7: Update Kernel Entry + Build System
- [ ] Phase 8: Cleanup + Documentation

---

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
│   - hal.inc (HAL_READ_PORT, etc.)      │
│   - Future: hal_cpu.inc, hal_mem.inc   │
├─────────────────────────────────────────┤
│   Architecture Layer (arch/x86/)        │  ← x86-specific implementations
│   - arch/x86/arch_io.inc (uses 'in'/'out')
│   - Future: arch/x86/arch_cpu.inc       │
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
1. **pic.inc** — 8259A PIC (`out` to PIC ports) → **DONE** (Phase 1)
2. **paging.inc** — Page tables, CR3, `invlpg` → arch/x86/ (Phase 4)
3. **idt.inc** — IDT gate descriptors (x86-specific structure) → arch/x86/ (Phase 3)
4. **syscall.inc** — Control characters (belongs in HAL, not arch)

### Medium Priority
5. **Port I/O** — Scattered `in`/`out` instructions → **DONE** (Phase 1)
6. **CPU control** — `cli`/`sti`, `lgdt`, `lidt` → arch/x86/cpu.inc (Phase 2)
7. **PIT/timer** — Channel 0 programming → arch/x86/timer.inc (Phase 5)

### Lower Priority (Already partially separated)
8. **VGA operations** — vga.inc (keep as-is, already separated)
9. **Keyboard controller** — i8042 operations (if present)

---

## Actual Directory Structure (After Phase 1)

```
kernel/src/
├── hal/                          ← HAL layer (architecture-agnostic interfaces)
│   ├── hal.inc                  ← Main HAL header (DONE)
│   └── Future: hal_cpu.inc, hal_mem.inc, etc.
├── arch/                         ← Architecture-SPECIFIC implementations
│   └── x86/                    ← x86 implementation
│       ├── arch_io.inc           ← x86 I/O (DONE - implements hal_io interface)
│       └── ata.inc              ← x86 ATA/IDE driver (moved from hal/)
├── includes/                     ← Original includes (now use HAL)
│   ├── pic.inc                  ← Migrated to HAL (DONE - uses HAL_PIC_* macros)
│   ├── syscall.inc              ← Syscall dispatcher (uses HAL interfaces)
│   ├── paging.inc               ← Page tables (to be migrated in Phase 4)
│   ├── idt.inc                 ← IDT (to be migrated in Phase 3)
│   └── ...                     ← Other includes
├── commands/                     ← Userland programs (use HAL via syscall only)
└── kernel.asm                   ← Kernel entry (uses HAL + arch/)
```

**For future ARM port:**
```
│   ├── arch/
│   │   ├── x86/                ← x86 implementation (existing)
│   │   └── arm/                ← ARM implementation (future)
│   │       ├── arch_io.inc       ; Memory-mapped I/O or co-processor
│   │       ├── arch_cpu.inc      ; cpsid/cpsie instructions
│   │       ├── arch_intr.inc     ; GIC (Generic Interrupt Controller)
│   │       ├── arch_mem.inc      ; ARM MMU (TTBR0, TTBCR, etc.)
│   │       ├── arch_timer.inc    ; ARM Generic Timer
│   │       └── arch_debug.inc    ; UART or framebuffer
```

---

## Completed Work (Phase 1 Details)

### Files Created
1. **`kernel/src/hal/hal.inc`** - Main HAL header
   - Architecture selection via numeric constants (1=x86, 2=ARM, 3=RISC-V)
   - Routes to correct `arch/<arch>/` implementation
   - Conditional includes based on `HAL_ARCH` define
   
2. **`kernel/src/arch/x86/arch_io.inc`** - x86 I/O implementation
   - Defines HAL macros using x86 `in`/`out` instructions
   - PIC macros: `HAL_PIC_WRITE_CMD`, `HAL_PIC_WRITE_DATA`, `HAL_PIC_READ_CMD`, etc.
   - PIT macros: `HAL_PIT_WRITE_CMD`, `HAL_PIT_WRITE_DATA`
   - UART, keyboard, and generic I/O macros
   - Memory-mapped I/O macros for future architectures

### Files Modified
3. **`kernel/src/includes/pic.inc`** - Migrated to HAL
   - Added `%include "hal/hal.inc"`
   - Replaced all `out PIC_*_CMD, al` with `HAL_PIC_WRITE_CMD master/slave`
   - Replaced all `out 0x43, al` with `HAL_PIT_WRITE_CMD`
   - Replaced `out 0x40, al` with `HAL_PIT_WRITE_DATA 0`

4. **`build/asm`** - Updated build script
   - Added `-I "${PROJECT_DIR}/kernel/src/hal/"`
   - Added `-I "${PROJECT_DIR}/kernel/src/arch/"`
   - Ensures NASM can find HAL files during kernel assembly

### Files Moved
5. **`kernel/src/arch/x86/ata.inc`** - Moved from `hal/` (x86-specific ATA code)

### Build Result
✅ **Kernel compiles successfully** (19911 bytes, 39 sectors)

### Commit History
- `909f13a` - HAL Phase 1: Infrastructure + IO Abstraction
- `b81882d` - Fix HAL directory structure: arch/ and hal/ now live under kernel/src/
- `7cb4978` - Add hal/ and arch/ to NASM include paths in build script

---

## Incremental Implementation Plan

### Phase 1: HAL Infrastructure + I/O Abstraction ✅ COMPLETED
**Goal**: Create directory structure and abstract port I/O

**Completed Steps:**
1. ✅ Created directory structure (`hal/`, `arch/x86/`)
2. ✅ Defined HAL I/O interface (`hal/hal.inc`)
3. ✅ Implemented x86 I/O (`arch/x86/arch_io.inc`)
4. ✅ Migrated `pic.inc` to use HAL macros
5. ✅ Updated build script with new include paths
6. ✅ Verified build (kernel compiles successfully)

---

### Phase 2: CPU Control Abstraction (NEXT)
**Goal**: Abstract interrupt control, GDT/IDT loading, etc.

#### 2.1 Define HAL CPU Interface (`hal/hal_cpu.inc`)
```nasm
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

; --- Cache/TLB control ---
%macro HAL_INVLPG 1           ; Invalidate TLB entry (x86 invlpg)
%macro HAL_FLUSH_CACHE 0      ; Flush CPU cache
%macro HAL_FLUSH_TLB 0        ; Flush entire TLB (write CR3 on x86)

; --- Halt/wait ---
%macro HAL_HALT 0             ; Halt CPU (hlt on x86, wfi on ARM)
%macro HAL_WAIT_FOR_INTR 0    ; Wait for interrupt (same as HAL_HALT on x86)
```

#### 2.2 Implement x86 CPU (`arch/x86/arch_cpu.inc`)
```nasm
%macro HAL_DISABLE_INTS 0
    cli
%endmacro

%macro HAL_ENABLE_INTS 0
    sti
%endmacro

%macro HAL_LOAD_GDT 1
    lgdt [%1]
%endmacro

%macro HAL_LOAD_IDT 1
    lidt [%1]
%endmacro

; ... etc.
```

#### 2.3 Migration Steps
1. Create `hal/hal_cpu.inc` with interface macros
2. Create `arch/x86/arch_cpu.inc` with x86 implementations
3. Replace `cli`/`sti` in kernel code with `HAL_DISABLE_INTS`/`HAL_ENABLE_INTS`
4. Replace `lgdt`/`lidt` with `HAL_LOAD_GDT`/`HAL_LOAD_IDT`
5. **IMPORTANT**: Do NOT replace cli/sti in userland (ring 3) — will #GP fault

**Verification**: Run scheduler, ensure timer ticks and context switches work

---

### Phase 3: Interrupt Controller Abstraction
**Goal**: Abstract PIC/APIC so we can swap 8259A for APIC or GIC (ARM)

#### 3.1 Define HAL Interrupt Interface (`hal/hal_intr.inc`)
```nasm
%macro HAL_INTR_INIT 0         ; Initialize interrupt controller
%macro HAL_INTR_MASK 1         ; Mask IRQ number (0-15)
%macro HAL_INTR_UNMASK 1       ; Unmask IRQ number (0-15)
%macro HAL_INTR_EOI 1         ; Send EOI for IRQ number (0-15)
; ... etc.
```

#### 3.2 Implement x86 PIC (`arch/x86/arch_intr.inc`)
- Move current `pic_remap` code here
- Wrap with HAL_INTR_* macros

**Verification**: Timer (IRQ0) and keyboard (IRQ1) still work

---

### Phase 4-8: Remaining Phases (See Original Plan)
- Phase 4: Memory Management Abstraction (paging)
- Phase 5: Timer Abstraction (PIT/APIC)
- Phase 6: Debug Output Abstraction (VGA/serial)
- Phase 7: Update Kernel Entry + Build System
- Phase 8: Cleanup + Documentation

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
   - `vga.inc` is ONLY for VGA hardware
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

## Next Steps

1. **Phase 2: CPU Control Abstraction**
   - Create `hal/hal_cpu.inc` with interface macros
   - Create `arch/x86/arch_cpu.inc` with x86 implementations
   - Migrate `cli`/`sti`/`lgdt`/`lidt` to HAL macros

2. **Test QEMU Boot** (Optional)
   - Verify OS still boots correctly with HAL changes from Phase 1
   - Run `./asm -r` to test in QEMU

3. **Future Architecture Ports**
   - With HAL in place, porting to ARM requires:
     - Create `arch/arm/` directory
     - Implement all HAL_* macros for ARM
     - Update build system for ARM cross-compilation
     - Test on ARM hardware or QEMU ARM system-mode

---

## Example: Before and After HAL (Phase 1 Results)

### Before (kernel code directly uses x86 instructions)
```nasm
; In pic.inc (set_freq):
mov al, 0x36
out 0x43, al              ; Direct PIT port write (x86-specific)
mov ax, PIT_DIVISOR
out 0x40, al              ; Low byte
mov al, ah
out 0x40, al              ; High byte
```

### After (kernel code uses HAL interfaces)
```nasm
; In pic.inc (set_freq) - NOW USES HAL:
mov al, 0x36
HAL_PIT_WRITE_CMD          ; Abstract PIT command write
mov ax, PIT_DIVISOR
HAL_PIT_WRITE_DATA 0       ; Abstract PIT data write (channel 0)
mov al, ah
HAL_PIT_WRITE_DATA 0       ; Abstract PIT data write (channel 0)
```

### Benefits
- Same `pic.inc` code works on any architecture
- x86 uses `out` instructions (in `arch/x86/arch_io.inc`)
- ARM would use memory-mapped I/O (in `arch/arm/arch_io.inc`)
- Kernel code stays identical across architectures

---

## Timeline Estimate (Revised After Phase 1)

- ~~Phase 1 (HAL IO + Infrastructure):~~ ✅ **COMPLETED** (3-4 hours actual)
- Phase 2 (CPU Control): 2-3 hours
- Phase 3 (Interrupt Controller): 3-4 hours
- Phase 4 (Memory Management): 4-5 hours
- Phase 5 (Timer): 2-3 hours
- Phase 6 (Debug Output): 2-3 hours
- Phase 7 (Kernel Entry + Build): 2-3 hours
- Phase 8 (Cleanup + Documentation): 3-4 hours

**Total Remaining**: ~20-25 hours

---

## Quick Reference: HAL File Locations

| File | Purpose | Status |
|------|---------|--------|
| `hal/hal.inc` | Main HAL header, arch selection | ✅ Done |
| `hal/hal_cpu.inc` | CPU control interface | ⏳ Phase 2 |
| `hal/hal_intr.inc` | Interrupt controller interface | ⏳ Phase 3 |
| `hal/hal_mem.inc` | Memory management interface | ⏳ Phase 4 |
| `hal/hal_timer.inc` | Timer interface | ⏳ Phase 5 |
| `hal/hal_debug.inc` | Debug output interface | ⏳ Phase 6 |
| `arch/x86/arch_io.inc` | x86 I/O implementation | ✅ Done |
| `arch/x86/arch_cpu.inc` | x86 CPU implementation | ⏳ Phase 2 |
| `arch/x86/arch_intr.inc` | x86 PIC implementation | ⏳ Phase 3 |
| `arch/x86/arch_mem.inc` | x86 paging implementation | ⏳ Phase 4 |
| `arch/x86/arch_timer.inc` | x86 PIT implementation | ⏳ Phase 5 |
| `arch/x86/arch_debug.inc` | x86 VGA/serial implementation | ⏳ Phase 6 |

---

**Document Version**: 2.0 (Added progress tracking after Phase 1 completion)  
**Last Updated**: June 6, 2026  
**Next Phase**: Phase 2 - CPU Control Abstraction
