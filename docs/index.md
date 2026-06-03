# x86 Assembly Kernel — Documentation

A small 32-bit operating system written entirely in NASM assembly. Boots from
BIOS, switches to protected mode with paging, runs round-robin tasks driven by
the PIT, and provides a shell with an in-kernel filesystem.

## Docs

- [Getting Started](getting-started.md) — dependencies, build, run
- [Architecture](architecture.md) — boot process, memory layout, subsystems
- [System Calls](syscalls.md) — full int 0x80 reference
- [Graphics](graphics.md) — VGA Mode 13h engine, palette, primitives, mouse
- [Filesystem](filesystem.md) — virtual FS layout, persistence, limits
- [Userland](userland.md) — writing and adding programs
- [ABI Contract](abi-contract.md) — position-independent binary rules
- [Debugging](debugging.md) — GDB, QEMU monitor, troubleshooting
- [Contributing](contributing.md) — code style, PR process

## Quick Start

```
./asm -r    # build + run in QEMU
./asm -f    # build + run fullscreen
./asm -d    # build + GDB server on localhost:1234
./asm       # build only
```

## Project Layout

```
asm                     # build script (delegates to build/asm)
setup.sh                # workspace setup and dependency check
kernel/
  bootloader.asm        # BIOS → protected mode
  kernel.asm            # main kernel entry
  constants.inc         # segment selectors, ports, addresses
  data.inc              # initialized globals
  bss.inc               # uninitialized globals
  paging.inc            # page table setup
  memory.inc            # physical frame allocator
  interrupt.inc         # IDT, IRQ handlers
  syscall.inc           # int 0x80 dispatch
  graphics.inc          # VGA Mode 13h engine + PS/2 mouse
  vfs.inc               # virtual filesystem core
  shell.inc             # VGA shell, line editor, command dispatch
  task.inc              # round-robin task switching
  exec.inc              # userland loading / ELF
commands/               # userland program sources (*.asm)
  pwd.asm ls.asm cd.asm cat.asm touch.asm write.asm rm.asm
  mkdir.asm rmdir.asm cp.asm mv.asm vi.asm ping.asm
  alloc.asm dealloc.asm peek.asm poke.asm dump.asm
  ps.asm exit.asm help.asm calc.asm argtest.asm
build/
  asm                   # the actual build logic
  gen_fs.py             # walks content dirs → fs.inc
  os.img                # final disk image
  bin/                  # compiled userland mirrors
  proc/ var/log/ etc/   # content mirrored into the kernel FS
proc/ var/ etc/         # host-side content sources
```
