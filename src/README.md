# Kernel Source Organization

The `src/` directory is organized into functional subsystems for clarity and maintainability.

## Directory Structure

### `boot/`
Early initialization and paging setup.
- `paging.asm` - Page directory/table initialization, kernel page reservation

### `hardware/`
Direct hardware abstraction layer (8259A, PIT, keyboard, disk, clock).
- `pic.asm` - Intel 8259A PIC remapping (IRQs → CPU vectors)
- `pit.asm` - Programmable Interval Timer (IRQ0) and frequency control
- `kbd.asm` - Keyboard controller (IRQ1), scancode translation, shift state
- `cmos.asm` - CMOS/RTC (real-time clock), epoch calculation
- `ata.asm` - ATA disk I/O (PIO mode, LBA)

### `interrupts/`
CPU exception and interrupt handling.
- `idt.asm` - IDT construction and descriptor setup
- `isr.asm` - Fault/exception handlers (page fault, general faults)

### `memory/`
Heap and physical memory management.
- `heap.asm` - Heap allocation/deallocation, alloc_table tracking
- (related: virtual page mapping lives in `boot/paging.asm`)

### `display/`
VGA text-mode framebuffer and output primitives.
- `vga.asm` - Cursor control, scrolling, character output, screen clear

### `shell/`
Interactive command-line shell (Task 0) and command dispatch.
- `shell.asm` - Main shell loop, command parsing, prompt
- `input.asm` - Keyboard input handling, command history, tab completion
- `commands.asm` - Built-in command table and dispatcher (`cmd_table`, `dispatch_command`)

### `tasks/`
Task scheduling and per-task entry points.
- `scheduler.asm` - Task switching logic, TCB management, context save/restore
- `task0.asm` - Shell task entry point
- `task1.asm` - Tick generator task entry point
- `task2.asm` - Bounce animation task entry point

### `syscalls/`
Linux-style `int 0x80` syscall interface (40+ syscalls).
- `syscall_interface.asm` - int 0x80 dispatcher, handler registration
- `syscall_io.asm` - Text output (putchar, print, newline, cls, etc.)
- `syscall_fs.asm` - Filesystem operations (stat, create, write, unlink, mkdir, rmdir)
- `syscall_mem.asm` - Memory operations (alloc, dealloc, peek, poke, read_mem)
- `syscall_process.asm` - Process/task management (getcwd, chdir, ps, regdump, kill)
- `syscall_util.asm` - Utility functions (hex2int, bin2hex, asc2int, memdump, etc.)

### `filesystem/`
Virtual filesystem and persistence.
- `fs_core.asm` - FS entry table, lookup, path resolution
- `fs_persist.asm` - Disk I/O for runtime-created files (spare slots at LBA 256+)

### `runtime/`
Global data, constants, and shared utilities.
- `data.asm` - All global variables, BSS sections, interrupt descriptors
- `strings.asm` - Hardcoded strings, keyboard mapping tables, keycodes
- `utils.asm` - Generic helper routines (int2str, bcd2bin, string utilities)

## Key Module Relationships

- **Boot**: `paging` → initialization of virtual memory
- **Hardware**: `pit` → irq0 → task switching via `scheduler`
- **Hardware**: `kbd` → irq1 → input ring buffer consumed by `shell`
- **Shell**: `input` ← keyboard buffer, `commands` → dispatch command execution
- **Syscalls**: All subsystems expose APIs via syscall numbers → registered in `syscall_interface`
- **Filesystem**: `fs_core` (lookup) + `fs_persist` (disk I/O)

## Building

Each `.asm` file is `%include`'d from the main `kernel.asm` in proper dependency order.
Order matters for forward references; syscall handlers reference functions from all subsystems.

## Contributing

When adding functionality:
1. Identify which subsystem it belongs to (or add a new one).
2. Place the code in the appropriate file.
3. Define exported symbols (function entry points) at the file start.
4. Use clear comments to separate logical sections within a file.
5. Keep related functionality in one file; split only when >500 lines.
