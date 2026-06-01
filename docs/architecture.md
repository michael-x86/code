# Architecture

## Boot Process

1. BIOS loads the 512-byte bootloader from disk to `0x7C00`
2. Bootloader (`bootloader.asm`):
   - Enables A20 line
   - Loads the kernel from disk via BIOS INT 13h
   - Sets up a flat 4 GB GDT (code + data segments)
   - Enters 32-bit protected mode
   - Copies the kernel to its load address
   - Jumps to the kernel entry point
3. Kernel (`kernel.asm`):
   - Sets up paging: identity-maps first 20 MB + higher-half at `0xC0000000+`
   - Initializes the IDT andPIC (IRQ0 = PIT at 100 Hz)
   - Creates tasks (task 2 = shell)
   - Enters the scheduler loop

## Memory Map (after paging)

```
0xC00B8000 – 0xC00B8FA0   VGA text framebuffer (80x25 cells)
0xC0100000 – 0xC0...      kernel image (code, data, bss, embedded FS)
0xC0800000 – 0xC1000000   heap (page-on-demand via `alloc` shell command)
```

A 32 KB `page_bitmap` tracks ownership of 1 GB of 4 KB physical frames.

## Subsystems

### Paging

- 4 KB pages, two-level page table (PDE → PTE)
- Identity map of first 20 MB + kernel virtual mapping at `0xC0000000+`
- Page-on-demand: `alloc` shell command triggers on-demand mapping

### Multitasking

- Round-robin via PIT IRQ0 (100 Hz)
- Each task has its own kernel stack
- Context switch saves/restores all general registers + EFLAGS
- Task 0: idle, Task 1: background, Task 2: shell

### Interrupts

- IDT with 256 entries; IRQ0 (timer) and IRQ1 (keyboard) remapped
- Keyboard scancodes feed a ring buffer
- Syscalls use interrupt gate (interrupts disabled during syscall)

### Virtual Filesystem

- Files are fixed-size records (68 bytes metadata + 1024-byte content buffer)
- Generated at build time by `gen_fs.py` from content directories
- Build-seeded files (`/proc`, `/etc`, `/var/log`) live inside the kernel image
- 16 "spare slots" for runtime-created files (persisted to disk)

### Shell

- VGA text mode 80x25, green-on-black
- Line history (up/down arrow keys)
- Tab completion for commands
- Built-in commands dispatched in kernel-space

### Userland

- Flat 32-bit binaries, max 4 KB, loaded into `/bin` at build time
- Entered via `int 0x80` syscall interface
- Return to shell via `ret`

## Disk Layout

```
LBA 0      — bootloader (512 bytes)
LBA 1..N   — kernel image
LBA 256+   — persisted filesystem spare slots (3 sectors per slot)
```

## Source Map

```
kernel/
  bootloader.asm    107 lines   BIOS entry → protected mode
  kernel.asm        243 lines   main init, service routines
  constants.inc     140 lines   segment selectors, ports, magic numbers
  data.inc           68 lines   initialized global data
  bss.inc           101 lines   uninitialized globals
  paging.inc        198 lines   page table / TLB management
  memory.inc        234 lines   physical frame bitmap allocator
  interrupt.inc     243 lines   IDT, PIC, IRQ handlers
  syscall.inc       761 lines   int 0x80 dispatch table & handlers
  vfs.inc           489 lines   filesystem operations (create, read, write, unlink)
  shell.inc        1174 lines   VGA driver, line editor, built-in commands
  task.inc          133 lines   context switch, task structs
  exec.inc          176 lines   ELF parsing, userland loading
```

Total: ~3,967 lines of assembly + 14 userland programs.
