---
paths:
 - "kernel/**"
 - "commands/**"
 - "build/asm"
 - "build/q*.py"
 - "build/debug-logs/**"
 - "docs/debugging.md"
---

# Debugging — Rules

**Last updated**: 2026-06-04

Applies to: GDB sessions, QEMU monitor use, log capture, and the QEMU helper scripts under `build/q*.py`. Read [docs/debugging.md](../docs/debugging.md) for the full guide; this file captures the rules an agent needs to follow.

## The Two-Lane Debug Cadence

| Lane       | Command             | Use it for                            | Bounded by |
|------------|---------------------|---------------------------------------|------------|
| Fast lane  | `./asm`             | assemble only, per micro-iteration    | <10s       |
| Boot lane  | `./asm -r`          | boot to prompt, per tranche           | <2s        |
| Debug lane | `./asm -d`          | GDB step-through, only when broken    | per session|

Default to the fast lane. Move to the boot lane at the end of a tranche. Move to the debug lane only when a previous lane has shown a defect.

## GDB Workflow

The QEMU GDB server is on `localhost:1234` and the kernel halts at the reset vector until you `continue` in GDB. There are no debug symbols — the kernel is a flat binary.

```bash
# Terminal 1
./asm -d

# Terminal 2
gdb
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) break *0xC0100000
(gdb) continue
(gdb) info registers
(gdb) stepi
```

### Key Addresses

| Address      | Where                                  |
|--------------|----------------------------------------|
| `0x7C00`     | Bootloader entry                       |
| `0xC0100000` | Kernel entry                           |
| `0xC00B8000` | VGA text framebuffer                   |
| `0xC00A0000` | VGA Mode 13h framebuffer (when active) |
| `0xC0800000` | Heap base                              |

To find the address of a routine, search `build/kernel.lst` (regenerated on every `./asm`). Pattern: `grep -n ' my_routine$' build/kernel.lst`.

### Breakpoint Recipes

```gdb
# Bootloader
break *0x7C00

# Kernel entry
break *0xC0100000

# A specific routine (find address in build/kernel.lst)
break *0xC0101234

# Watch a memory location for writes
watch *(int *)0xC0105678

# Conditional breakpoint (skip the first 1000 hits)
break *0xC0101234 if $ecx > 1000
```

### When GDB Hangs

- QEMU halts at the reset vector until you `continue`. If GDB seems to hang, you forgot `continue`.
- QEMU exits when the kernel calls the `shutdown` syscall. If you're stepping and the process disappears, you hit the shutdown path. Set a breakpoint on `int 0x80` and check `eax`.
- Triple faults look like QEMU exiting with `KVM: entry failed, hardware error 0`. The fault address is in the kernel log. Set a breakpoint earlier in the suspect routine and step.

## QEMU Monitor

Add `-monitor stdio` to the QEMU invocation (temporarily edit `build/asm` or run QEMU manually) for an interactive monitor. Useful commands:

```
info registers      # dump CPU state at the fault
info mem            # show mapped memory
info tlb            # show TLB entries
info pic            # PIC state
xp /16xb 0xC0100000 # examine physical memory
xp /16xw 0xC0100000 # examine as 32-bit words
```

For scripted monitor sessions, use the `build/q*.py` helpers (qint, qmem, qstack, qstate, qtrace). They wrap the monitor protocol for common inspection tasks.

## Log Capture

For offline inspection of a boot or debug session:

```bash
# Boot lane
./asm -r 2>&1 | tee build/run.log

# Debug lane
./asm -d 2>&1 | tee build/debug-logs/session-YYYYMMDD-HHMMSS.log
```

When capturing logs:

- Always redirect to a file under `build/` (gitignored)
- Use the date/time in the filename for sortability
- Cap at 5 MB per log; rotate, don't grow unbounded
- Do not ingest whole logs into context — narrow with `rg "<pattern>" build/debug-logs/<file>` or the code index

## Troubleshooting Table (from docs/debugging.md)

| Symptom                       | Likely cause                          | First check                                     |
|-------------------------------|---------------------------------------|-------------------------------------------------|
| QEMU triple fault             | Paging or GDT misconfiguration        | `info registers` in QEMU monitor, look at `cr3` |
| No keyboard input             | IRQ1 not enabled / IDT entry missing  | `info pic`, check `IRQ1` mask                   |
| Screen garbage                | VGA segment register wrong            | Dump framebuffer at `0xC00B8000`                 |
| Filesystem empty              | Content dirs missing / `gen_fs.py` err | Check `build/fs.inc` exists, content dirs present |
| Persistence not working       | ATA PIO timing / wrong drive select   | Check `fs_ata_base`/`fs_ata_drive` in `constants.inc` vs bootloader handoff |
| Userland crash                | Bad syscall args / buffer overflow    | `break *<exec_vbase>` and step into the call    |
| "command not found"           | `commands/<name>.asm` missing from `COMMANDS=(...)` in `build/asm` | Append it, rerun `./asm`                        |
| `int 0x80` returns `-1`       | Bad syscall number                    | Compare against [docs/syscalls.md](../docs/syscalls.md) |
| PIT-driven hangs              | `disk_busy` or IF-handling regression | Read the `disk_busy` handoff in `ai-context/handoffs.jsonl` |

## Repro Workflow (When You Have a Bug Report)

1. **Confirm clean state**: `git status` should be clean (or all changes intentional). `git log --oneline -5` to see recent context.
2. **Reproduce on main**: `./asm -r`, run the failing scenario.
3. **Bisect if recent**: `git bisect start; git bisect bad; git bisect good <known-good-sha>`. Re-run `./asm -r` at each step.
4. **Capture the smallest failing case**: if it takes N commands to trigger, capture all N in a script under `build/` (gitignored).
5. **Inspect**: read the relevant `docs/*.md` (architecture, abi-contract, syscalls, filesystem as appropriate), then look at the suspect file.
6. **Step**: use `./asm -d` to step through the suspect routine. Set the breakpoint at the routine's start address from `build/kernel.lst`.
7. **Fix minimally**: smallest possible change. Don't "clean up" surrounding code in the same commit.
8. **Validate**: `./asm` then `./asm -r`, run the failing scenario, confirm fixed.
9. **Handoff**: write a handoff entry to `.clinerules/ai-context/handoffs.jsonl` if the bug was subtle, with the bisect SHA, the fix, and the test.

## Anti-Patterns (Don't)

- Don't step through the whole kernel from `0xC0100000` looking for the bug. Find the suspect routine first, set a breakpoint at its entry, then step.
- Don't guess at addresses — use `build/kernel.lst`.
- Don't ignore NASM warnings hoping they're harmless — every warning is a defect, and the one you skip is the one that bites.
- Don't capture logs to `/tmp` — use `build/debug-logs/` so they are gitignored and easy to find.
- Don't "fix" the disk_busy interaction by toggling IF — read the regression handoff first.
- Don't add a `-g` flag to NASM and expect source-level GDB — the kernel is a flat binary. Use addresses from `build/kernel.lst`.

## Reference

- [docs/debugging.md](../docs/debugging.md) — full guide
- [docs/architecture.md](../docs/architecture.md) — memory map, subsystem layout
- [docs/syscalls.md](../docs/syscalls.md) — syscall numbers for `int 0x80` breakpoints
- `build/kernel.lst` — assembly listing (the address book)
- `build/q*.py` — QEMU scripting helpers
- `.clinerules/ai-context/handoffs.jsonl` — past debugging sessions, root causes, fixes

---

**Last updated**: 2026-06-04
