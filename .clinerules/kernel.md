---
paths:
 - "kernel/**"
 - "original/bootloader.asm"
 - "original/kernel.asm"
---

# Kernel Sources — Rules

**Last updated**: 2026-06-04

Applies to: `kernel/`, `kernel/src/`, `kernel/src/includes/`, and the archived `original/`.

## The Kernel Contract

This is a **32-bit protected-mode kernel** at higher-half `0xC0100000`, identity-mapping the first 20 MB, with paging, IDT, PIC, PIT, ATA, and an in-kernel VFS. Every line you write in `kernel/src/` participates in that contract. Read [docs/architecture.md](../docs/architecture.md) before editing.

### Memory Map (do not break this)

```
0xC00A0000 – 0xC00A0000 + 0x10000   VGA Mode 13h framebuffer (when active)
0xC00B8000 – 0xC00B8FA0             VGA text framebuffer (80x25)
0xC0100000 – 0xC0...                kernel image (code, data, bss, embedded FS)
0xC0800000 – 0xC1000000             heap (page-on-demand via `alloc`)
```

Identity map of the first 20 MB at low addresses **and** at `0xC0000000+` is mandatory — the bootloader hands off to `0xC0100000` and any unmapped reference will triple-fault.

### Bootloader → Kernel Handoff (do not break this)

Stored by the bootloader at fixed physical addresses and consumed by the kernel entry:

| Address  | Field            | Notes                                           |
|----------|------------------|-------------------------------------------------|
| `0x0500` | `boot_drive`     | BIOS INT 13h boot drive number                  |
| `0x0504` | `fs_ata_base`    | PIO base port for FS disk (e.g. `0x01F0`)       |
| `0x0508` | `fs_ata_drive`   | ATA drive select byte (e.g. `0xF0` = primary slave) |
| `0x050C` | `fs_base_lba`    | LBA where the FS region starts                  |

Do not move these without updating both `bootloader.asm` and the kernel consumers in the same commit. Read `kernel/bootloader.asm` and the relevant lines in `kernel/src/includes/constants.inc` first.

## NASM Code Style

Follow [docs/contributing.md](../docs/contributing.md) and the existing files. Key points:

- **Labels**: `lowercase_with_underscores` (e.g. `setup_paging`); local labels with leading `.` (e.g. `.check_loop`)
- **Constants / EQU**: `UPPERCASE_WITH_UNDERSCORES` (e.g. `CODE_SEG`, `DATA_SEG`, `KERNEL_VBASE`)
- **Section headers**: `; --- Section Name ---` (one blank line above and below)
- **Function header** (required for every exported routine):
  ```
  ; --- Brief description ---
  ; in:  eax = input description
  ;      ebx = input description
  ; out: eax = return value
  ;      CF = 1 on failure
  my_function:
      push ebx
      ...
  ```
- **Comments**: explain *why*, not *what*. State hardware preconditions, side effects on registers/flags, and any interrupt-mask state.
- **No NASM warnings**. If NASM warns, you have a defect — fix it before committing. Do not silence with `[-w+xxx]`.

## Register and Flag Discipline

This is the substitution for the rugged-laptop "Rust memory model" rule. NASM gives you no safety net, so the contract is in your head and in the comments.

### Caller-saved vs callee-saved (32-bit cdecl-ish)

| Register  | Saved by | Notes                                              |
|-----------|----------|----------------------------------------------------|
| `eax`     | caller   | return value / scratch                             |
| `ebx`     | callee   | must be preserved                                  |
| `ecx`     | caller   | scratch (4th syscall arg)                          |
| `edx`     | caller   | scratch (3rd syscall arg)                          |
| `esi`     | callee   | must be preserved (5th syscall arg)                |
| `edi`     | callee   | must be preserved (6th syscall arg)                |
| `ebp`     | callee   | frame pointer                                      |
| `esp`     | —        | always valid; ring-0 stack per task                |

Document the contract in the function header. Push/pop every callee-saved register you touch.

### Flags

`cli` / `sti` is your friend and your enemy. State the interrupt state at every entry/exit boundary. Ring transitions and the syscall gate must preserve IF correctly (interrupt gate clears IF; trap gate preserves it). The `disk_busy` regression in commit `67a8af2` was caused by replacing `sti` with a flag and using a trap gate where the original used an interrupt gate — read its handoff entry under `.clinerules/ai-context/handoffs.jsonl` before touching ATA.

### Segment registers

`ds`, `es`, `fs`, `gs` must be valid selectors pointing to the flat data segment for any memory access. `cs` is `CODE_SEG` (0x08). `ss` is `DATA_SEG` (0x10). Do not load garbage into a segment register; you will triple-fault and lose state.

## Include Order

When adding a new `.inc` to `kernel/src/includes/`:

1. `constants.inc` first (EQUs, addresses, ports)
2. `data.inc` second (initialized globals)
3. `bss.inc` third (uninitialized globals)
4. Subsystem includes (`paging.inc`, `interrupt.inc`, …) in dependency order
5. Reference the new file from the include block in `kernel/src/kernel.asm`

Add a `; --- <name> ---` section header to your include file the first time you create it.

## Paging Invariants

- The kernel page directory lives at a fixed physical address (see `paging.inc`). Every new mapping must update it **and** invalidate the corresponding TLB entry with `invlpg`.
- `alloc` shell command triggers on-demand mapping; if you add a new mapping helper, follow the same `find_free_frame` + `map_page` discipline.
- Never map a page both as writable and as an IDT/GDT page. Never mark kernel pages as user-accessible. The kernel runs at ring 0; userspace is ring 0 in this codebase (no ring-3 support), but the bits are still reserved correctly in PDEs/PTEs — do not skip the work.

## IDT and GDT Discipline

- Every IDT entry must specify: gate type (interrupt `0x8E` vs trap `0x8F` vs task `0x85`), DPL (`0` for ring-0, `3` for syscall gate), and the correct handler offset/selector.
- Document the reason for each gate type in a comment. Don't "just use 0x8E everywhere" — `disk_busy` regression showed the cost.
- Adding a new IRQ handler: register the handler in `interrupt.inc`, mask/unmask the IRQ on the PIC, and document the hardware source.
- GDT changes are rare and dangerous. If you must add a segment, document its purpose, base, limit, DPL, and the new GDT layout. Re-check every `jmp far`/`call far` that targets it.

## ATA / PIO Path

- PIO is polling. Timeouts must be bounded — the PIT ticks at 100 Hz, so a 1-second timeout is ~100 ticks.
- `disk_busy` and similar flags must preserve the original IF discipline. Read the existing `ata_read`/`ata_write` carefully before "optimizing" it.
- Persisted files live in the spare-slot region at LBA `256 + slot*3` (3 sectors per slot). The `gen_fs.py` build step backs up the FS region across `./asm` rebuilds, so user files normally survive — do not change the persistence layout without updating `gen_fs.py` and `docs/filesystem.md` in the same commit.

## Task and Scheduler Discipline

- Round-robin at 100 Hz via PIT IRQ0. Each task has its own ring-0 stack.
- `task_switch` saves/restores the full register set and segment registers. Do not "optimize" by skipping registers — the userland ABI assumes a clean register state on entry.
- Task 2 is the shell by convention. Don't reassign it without updating the shell init in `kernel/src/includes/shell.inc`.

## Validation Gate

Before any kernel change ships:

1. `./asm` — clean assemble, no warnings
2. `./asm -r` — boots to a clean prompt
3. Exercise the affected path (run the relevant shell command, syscalls, or task)
4. If you changed the IDT, GDT, paging, or ATA path: run `./asm -d` and step through the new code with GDB before declaring done
5. If you changed the persistence layout: confirm runtime-created files survive `./asm` (rebuild boots and your file is still there)

## Anti-Patterns (Don't)

- Don't add `cli` / `sti` pairs around small sections without a comment saying *why* and *what could interrupt*
- Don't store pointers in segment registers
- Don't trust user-supplied pointers in the syscall path — see `.clinerules/syscalls.md`
- Don't add ring-3 support without writing the new TSS, switching the syscall gate to DPL=3, and updating every page-table user bit
- Don't "fix" the disk_busy interaction by toggling IF — read the regression handoff first

---

**Last updated**: 2026-06-04
