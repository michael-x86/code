# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A 32-bit x86 operating system written entirely in NASM assembly. No libc, no runtime, no external abstractions. Boots via BIOS, enters protected mode, runs the shell as a single task driven by the PIT at 100 Hz, and exposes a green-on-black VGA shell with a persistent in-kernel filesystem.

The `webulator/` subdirectory is a browser-based x86 PC emulator that runs the OS kernel. It emulates a full x86 machine with CPU, paged memory, VGA text mode, PIC/PIT, PS/2 keyboard, ATA disk, BIOS INT services, and ACPI tables.

## Build Commands

```bash
./asm            # assemble only (the fast lane, <10s — use after every micro-edit)
./asm -r         # assemble + boot in QEMU window (boot lane, use per tranche)
./asm -f         # assemble + boot fullscreen (Ctrl+Alt+F to release cursor)
./asm -d         # assemble + QEMU with GDB server on localhost:1234, halts at start
./asm -i         # rebuild local code index → build/code-index/code-index.sqlite

make <name>      # compile a single userland program (e.g. make cat)
make all         # compile all userland programs
```

`./asm` is a thin wrapper; **all build logic lives in `build/asm`**. Never edit `./asm`; edit `build/asm`.

NASM warnings are stop-the-line defects — every warning must be fixed before merge.

## Debugging

```bash
# Terminal 1
./asm -d

# Terminal 2
gdb
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) break *0xC0100000     # kernel entry
(gdb) continue
(gdb) stepi
```

Find routine addresses in `build/kernel.lst` (regenerated on every `./asm`):
```bash
grep -n ' my_routine$' build/kernel.lst
```

Key addresses: `0x7C00` = bootloader, `0xC0100000` = kernel, `0xC00B8000` = VGA framebuffer, `0xC0800000` = heap base.

Use `build/q*.py` helpers (qshot, qkeys, qmem, qstack, qstate, qtrace, qvga) for scripted QEMU inspection. Add `-monitor stdio` to the QEMU invocation in `build/asm` for an interactive QEMU monitor.

## Architecture

### Boot flow
BIOS → `kernel/bootloader.asm` (A20, flat GDT, protected mode, copies kernel) → `kernel/src/kernel.asm` (paging, IDT/PIC, single task, scheduler loop).

### Memory map (after paging)
```
0xC00B8000  VGA text framebuffer (80×25)
0xC0100000  kernel image (code + data + bss + embedded FS)
0xC0800000  heap (page-on-demand)
```

### Disk layout
```
LBA 0       bootloader (512 bytes)
LBA 1..N    kernel image
LBA 256+    persistence region (16 spare slots, 3 sectors each)
```

### Kernel source map (`kernel/src/`)
| File | Responsibility |
|------|----------------|
| `kernel.asm` | Entry, init, service stubs |
| `constants.inc` | Segment selectors, ports, magic numbers |
| `paging.inc` | Two-level page tables, TLB |
| `memory.inc` | Physical frame bitmap allocator |
| `interrupt.inc` | IDT, PIC, IRQ handlers |
| `syscall.inc` | `int 0x80` dispatch (22 syscalls) |
| `vfs.inc` | VFS CRUD, persistence to ATA |
| `shell.inc` | VGA driver, line editor, built-in commands |
| `task.inc` | Round-robin context switch |
| `exec.inc` | Userland loader (position-independent patching) |

### Filesystem
Files are 68-byte metadata records + 1024-byte content buffers, generated at build time by `build/gen_fs.py` from `kernel/etc/`, `kernel/proc/`, `kernel/var/log/`. Build-seeded files reset on boot; the 16 spare slots persist to disk and survive `./asm` rebuilds (the build script backs up/restores the persistence region). Never edit `build/fs.inc` by hand.

### Userland (`commands/*.asm`)
Flat 32-bit binaries capped at 64 KB, assembled with `[bits 32] [org 0x00000000]`. The loader patches `ebp` to the actual load address — **all data references must use `lea reg, [ebp + label]`**, never `mov reg, <label>`. Programs communicate with the kernel only via `int 0x80` and return to the shell with `ret`.

To add a new program:
1. Write `commands/<name>.asm` following the header convention in `.clinerules/commands.md`
2. Append `<name>` to `COMMANDS=(...)` in `build/asm` (alphabetical)
3. `./asm` — confirm `build/bin/<name>` is created under 64 KB
4. `./asm -r` — boot and invoke it

## Key Conventions

- **`int 0x80` syscall ABI**: `eax` = number, args in `ebx ecx edx esi edi`, return in `eax`. Bad number → `-1`. See `docs/syscalls.md` for the full table.
- **Code style**: `; --- Section Name ---` headers, `; in:` / `; out:` on function headers, lowercase underscore labels (`.local` for locals), UPPERCASE `EQU` constants.
- **No absolute addresses** in userland — file offsets point into the IDT at runtime.
- After any source-layout change (added/moved `.asm` or `.inc`), run `./asm -i` to refresh the code index.
- Documentation lives in `docs/`; update it and bump "Last updated" on every touched file.
- All build artifacts go under `build/` (gitignored). Never commit `build/os.img`, `build/kernel.bin`, `build/fs.inc`, or anything under `build/bin/`.

## Pre-PR Checklist

There is no CI. These checks are the contract:

- [ ] `./asm` — no NASM warnings, no errors
- [ ] `./asm -r` — clean boot to prompt, affected commands work, clean exit
- [ ] Runtime-created files survive a `./asm` rebuild (persistence region intact)
- [ ] Relevant `docs/*.md` updated with "Last updated" bumped
- [ ] No `build/` artifacts staged
- [ ] Any new syscall added to `docs/syscalls.md` and exercised by a test caller
- [ ] `./asm -i` run if source layout changed

## Detailed Documentation

| Topic | File |
|-------|------|
| Architecture, memory map, boot flow | `docs/architecture.md` |
| Syscall table | `docs/syscalls.md` |
| VFS layout and persistence | `docs/filesystem.md` |
| Userland ABI contract | `docs/abi-contract.md` |
| GDB + QEMU debugging guide | `docs/debugging.md` |
| Adding programs | `docs/userland.md` |
| VGA Mode 13h graphics | `docs/graphics.md` |
| Code style and PR process | `docs/contributing.md` |

Path-specific agent rules: `.clinerules/kernel.md`, `.clinerules/commands.md`, `.clinerules/syscalls.md`, `.clinerules/build.md`, `.clinerules/debugging.md`, `.clinerules/graphics.md`.

Session handoffs and known bug history: `.clinerules/ai-context/handoffs.jsonl`.
