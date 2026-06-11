If you are looking for clean APIs and high-level comfort, this is not it.

If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.

# x86 Assembly Kernel

A small 32-bit operating system written entirely in NASM assembly. Boots from BIOS, switches to protected mode with paging, runs three round-robin tasks driven by the PIT, and gives you a green-on-black VGA shell with persistent filesystem.

Built from scratch on Linux with no libc, no runtime, and no external abstractions.

```
$ ls
bin/  proc/  var/  etc/
$ cd /proc
$ cat cpuinfo
processor      : 0
vendor_id      : Cyberdyne Systems
cpu family     : Neural-Net Processor
model          : T-800 Series 101
model family   : Skynet
stepping       : Version 2.4
flags          : learning infiltration phased-plasma
```

## Features

- **x86 Bootloader** → **32-bit Protected Mode** → **Paging** → Higher-half kernel at `0xC0100000`
  - Identity mapping of first 4 MB + kernel low page table (0x00400000–0x007FFFFF)
  - Heap page tables (3 tables covering 0x00800000–0x013FFFFF)
  - Virtual memory split: identity map at PDE[0] and higher-half at PDE[768+]
  
- **Task Scheduling**: Three round-robin tasks driven by IRQ0 (PIT at configurable frequency)
  - **Task 0**: Interactive shell (polling-based input)
  - **Task 1**: Ticker task (tick generator)
  - **Task 2**: Bounce animation (bouncing)

- **VGA Text Shell** (80×25)
  - Line history with arrow keys (up/down)
  - Tab completion for commands
  - Real-time hardware clock display (Hz, tick count) in banner

- **In-Kernel Virtual Filesystem**
  - Generated at build time from project directory by `gen_fs.py`
  - 68-byte fixed-size file records embedded in kernel image
  - 1024-byte mutable buffers for runtime content
  - 16 "spare slots" for runtime-created files

- **Persistent Filesystem**
  - Read/write via `touch`, `write`, `rm`, `mkdir`, `rmdir` commands
  - Persistence to disk via **PIO ATA** (LBA 256+) — files survive reboot AND kernel rebuilds
  - Build script automatically backs up and restores the FS region

- **Linux-Style int 0x80 Syscall Interface** (39+ syscalls)
  - Text output: `putchar`, `print`, `print_n`, `newline`, `cls`
  - Input: `get_key`, `get_tick`
  - Filesystem: `stat`, `list_dir`, `create`, `write`, `unlink`, `mkdir`, `rmdir`
  - Memory: `alloc`, `dealloc`, `peek`, `poke`, `read_mem`
  - Process: `getcwd`, `chdir`, `get_arg`, `ps`, `dump`
  - Utilities: `hex2int`, `bin2hex`, `asc2int`, `memdump`, `plot`
  - Display: `print_hex`, `print_int`, `banner`, `tick`, `hertz`, `bounce`
  - System: `shutdown`

- **Userland Binaries**
  - Linked into `/bin` and loaded on demand
  - ≤4 KB flat binaries, assembled separately
  - Communicate with kernel only via `int 0x80`

- **Minimal Modal vi Editor**
  - `löäp` movement, `i` insert, `ESC` exit insert, `x` delete, `w` save, `q` quit
  - Single-buffer editing (no scroll; 24-line display limit)

## Quick Start

Requires: `nasm`, `python3`, `qemu-system-i386`

```bash
./asm -r          # build + run in QEMU window
./asm -f          # fullscreen mode
./asm -d          # GDB server on localhost:1234, halts at start
./asm             # build only
```

## Architecture at a Glance

### **Bootloader** (`bootloader.asm`)
- Loads kernel from disk into linear memory
- Sets up a flat 4 GB GDT with CODE_SEG (0x08) and DATA_SEG (0x10)
- Enters protected mode (CR0.PE = 1)
- Copies kernel to its final load address (`0xC0100000`)
- Jumps to kernel entry point

### **Kernel** (`kernel.asm`)
- **Paging Setup** (`page_mapping`)
  - Identity-maps first 4 MB (PDE[0] → `identity_page_table`)
  - Maps kernel code+data (PDE[1] → `kernel_low_page_table`, phys 0x00400000–0x007FFFFF)
  - Sets up 3 heap page tables (PDEs 2–4, PDE[770–772]) for on-demand allocation
  - Dual-maps kernel in higher half (PDE[768–772]) for virtual addressing
  - Page directory at `0xC0000000`, page tables follow
  - All PTE entries OR'd with 0x3 (present | writable)

- **Memory Bitmap** (`page_bitmap`)
  - 32 KB bitmap tracking 1 GB of 4 KB physical frames
  - `reserve_kernel_pages()` protects kernel's own pages from allocation
  - Page-on-demand allocation via `alloc()` syscall

- **Interrupt Handlers**
  - `build_idt`: Constructs 256-entry IDT
  - `set_irq0`: PIT timer interrupt (configurable frequency)
  - `set_irq1`: Keyboard IRQ, feeds into ring buffer (`kbd_buf`)
  - `set_syscall`: INT 0x80 handler for userland
  - `pic_remap`: Remaps PIC IRQs to INT 32–47

- **Task Switching** (called on each IRQ0)
  - Round-robin through 3 tasks
  - Each task has its own stack (`task0_esp`, `task1_esp`, `task2_esp`)
  - Context saved/restored via `pushad`/`popad`

- **Shell** (Task 0, `kernel_main` / `.task0_entry`)
  - Polls for keyboard input
  - Parses arguments into `argv[]` / `argc`
  - Dispatches builtin commands or loads userland binaries
  - Banner shows Hz, tick count, system message

- **Display**
  - VGA framebuffer at `0xC00B8000` (80×25 text cells, 2 bytes each: char + color)
  - Cursor position tracked in `cursor_pos`
  - Hardware cursor updated via I/O ports 0x3D4–0x3D5 (CRT controller)
  - Scrolling when bottom of screen is exceeded

- **Clock** (`hwclock`)
  - Reads CMOS RTC (port 0x70/0x71)
  - Computes Unix epoch timestamp
  - Accounts for leap years (divisible by 4, except centuries unless divisible by 400)

### **Filesystem** (`fs.inc` — auto-generated by `gen_fs.py`)
- 68-byte fixed-size file records embedded in kernel image
- Each record: inode number, mode (dir/file), size, name, content pointer
- 1024-byte mutable content buffers for each file
- 16 "spare slots" at end for runtime-created files
- All pointers are embedded offsets (flat, no indirection)

### **Persistence**
- Modified spare-slot files are written to fixed region at **LBA 256** (`3 sectors per file = 16 × 3 = 48 sectors`)
- At boot, `load_fs_persist` replays the on-disk files into the spare slots
- Build script (`./asm`) backs up FS region before reassembly, restores after
- Survives both reboots and kernel rebuilds

### **Userland**
- Programs assembled as `[bits 32]`, `[org 0x00000000]`, flat binaries
- Maximum 4 KB (rest zero-filled)
- Compiled into `bin/<name>` at build time
- Loaded into memory on first invocation, executed with `call` gate

## File Layout

```
├── bootloader.asm           # 16→32 bit boot stub
├── kernel.asm               # kernel + paging + tasks + shell + 37+ syscalls
├── fs.inc                   # auto-generated: embedded filesystem records
├── gen_fs.py                # walks project dir, emits fs.inc
├── asm                      # build script
├── commands/                # userland program sources
│   ├── pwd.asm ls.asm cd.asm cat.asm
│   ├── touch.asm write.asm rm.asm
│   ├── vi.asm mkdir.asm
│   └── (and more...)
├── bin/                     # compiled userland binaries
├── proc/                    # content mirror (host-side)
├── var/log/                 # content mirror (host-side)
└── etc/                     # content mirror (host-side)
```

## Adding a Program

1. Write `commands/<name>.asm`:
   ```asm
   [bits 32]
   [org 0x00000000]
   
   ; your code here
   ; use only int 0x80 to communicate with kernel
   
   mov eax,0
   int 0x80
   ```

2. Append `<name>` to the `COMMANDS=(…)` array in the `asm` script.

3. Run `./asm`. Binary lands at `bin/<name>` and is callable by typing `<name>` at the shell prompt.

**Constraints**: Max 4 KB per program (rest zero-filled in flat-binary mode).

## int 0x80 Syscall Table

`eax` = syscall number, arguments in `ebx/ecx/edx/esi/edi`, return in `eax`. Invalid syscall → `-1`.

Syscalls run with interrupts off (interrupt gate), so PIT cannot preempt them.

| # | Name | Args | Returns |
|----|------|------|---------|
| 0 | exit |  | eax |
| 1 | print | esi = ptr (null-term) | 0 |
| 2 | print_cr | esi = ptr (CR=13 → newline) | 0 |
| 3 | newline | — | 0 |
| 4 | cls | — | VGA cleared |
| 5 | print_hex | ebx | 0 |
| 6 | print_int | ebx (decimal) | 0 |
| 7 | get_key | — | ASCII (0 if empty) |
| 8 | get_tick | — | PIT tick count (100 Hz) |
| 9 | shutdown | — | (does not return) |
| 10 | read_mem | ebx = addr | dword at [addr] |
| 11 | getcwd | edi = dst | 0 |
| 12 | chdir | esi = path | 0 / -1 |
| 13 | list_dir | ebx = idx, edi = dst | type / -1 (writes basename) |
| 14 | get_arg | ebx = idx, edi = dst | 0 / -1 (writes argv[i]) |
| 15 | stat | esi = path, edi = info(12B) | 0 / -1 |
| 16 | print_n | esi = ptr, ecx = n | 0 (`\n`→newline, `\t`→space) |
| 17 | create | esi = path | 0 / -1 |
| 18 | write | esi = path, ebx = buf | 0 / -1 |
| 19 | unlink | esi = path | 0 / -1 |
| 20 | mkdir | esi = path | 0 / -1 |
| 21 | rmdir | esi = path | 0 / -1 |
| 22 | ps | — | current processes |
| 23 | dump | — | stack and registers |
| 24 | alloc | ecx = bytes (+4096) | ptr → heap memory |
| 25 | dealloc | ebx = ptr | 0 / -1 |
| 26 | peek | ebx = addr | dword at [addr] |
| 27 | poke | ebx = addr, ecx = value | 0 |
| 28 | hex2int | esi → string | eax = integer |
| 29 | banner | — | print banner |
| 30 | bounce | — | toggle bounce animation |
| 31 | bin2hex | filename  | out: HEX |
| 32 | memdump | ebx = addr, ecx = bytes | print memory |
| 33 | hexbyte | ebx = byte | print BYTE as hex |
| 34 | asc2int | esi → string | eax = integer (atoi) |
| 35 | hertz | — | print CPU Hz in banner |
| 36 | tick | — | print heartbeat tick |
| 37 | plot | ebx = x, ecx = y | plot at (x, y) on 80×25 grid |
| 38 | epoch |                 | ebx=seconds |
| 39 | putchar | ebx = char | print char|

## Memory Map (After Paging Active)

```
0x00000000 – 0x003FFFFF   Physical identity-map (first 4 MB)
0xC00B8000 – 0xC00B8FA0   VGA text framebuffer (80×25 cells, 2 bytes each)
0xC0000000 – 0xC03FFFFF   Higher-half identity-map (mirrors physical 0x00000000–0x003FFFFF)
0xC0400000 – 0xC07FFFFF   Kernel code+data+bss+embedded FS
0xC0800000 – 0xC1000000   Heap (on-demand paging via alloc)
```

**Page Bitmap**: 32 KB at end of kernel, tracks ownership of 1 GB of 4 KB frames.

## Built-In Shell Commands

| Command | Description |
|---------|-------------|
| `heap` | show allocated memory pointers |
| `frequency` | set/reset frequency |
| `epoch`     | print epoch time |
## Userland Programs (`/bin/`)

| Program | Description |
|---------|-------------|
| `pwd` | print working directory |
| `ls` | list current directory |
| `cd <path>` | change directory (absolute, `.`, `..`, or single relative name) |
| `cat <file>` | print file contents (renders `\n` and `\t`) |
| `touch <file>` | create empty file |
| `write <file> <text...>` | append text to file |
| `mkdir <name>` | create directory |
| `rmdir <name>` | remove empty directory |
| `cp <src> <dest>` | copy file |
| `mv <src> <dest>` | move/rename file |
| `rm <file>` | delete file |
| `vi <file>` | minimal editor (hjkl, i, ESC, x, w, q) |
| `ping` | int 0x80 liveness test |
| `alloc <bytes>` | allocate heap memory (min 4 KB) |
| `dealloc <addr>` | release allocated memory |
| `peek <addr>` | read 32-bit value from memory |
| `poke <addr> <value>` | write 32-bit value to memory |

## Debugging with GDB

```bash
./asm -d                              # Start with GDB server
gdb                                   # In another terminal
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) continue
```

No debug symbols (flat binary). Use raw addresses: `0x7C00` (bootloader), `0xC0100000` (kernel).

## Known Limitations

- **Ring 0 only** — no userspace memory protection. Buggy programs can corrupt the kernel.
- **Path resolution** handles absolute paths, `.`, `..`, and single relative names only. `cd foo/bar` not supported.
- **File content** capped at 1024 bytes (`FS_CAPACITY`).
- **Runtime files** limited to 16 spare slots (`FS_SPARE_COUNT`).
- **BIOS boot** reads kernel in one call; assumes contiguous sectors.
- **ls** ignores arguments — always lists current directory.
- **vi** has no scroll; files longer than 24 lines get truncated on display.
- **ATA persistence** assumes primary IDE master is the boot disk.
- **I/O is synchronous** — ATA is PIO polling, keyboard uses IRQ-fed ring buffer (no interrupt-driven I/O).
- **Paging tables** are fixed at assembly time; no dynamic table allocation.

## Design Philosophy

### Zero Abstraction
If it is not explicitly written, it does not exist.

### Instruction-Level Control
Every register, flag, interrupt frame, and memory mapping is visible and direct.

### Hardware-First Engineering
The kernel is designed around CPU behavior and hardware constraints, not software convention.

---

## Development Status

In irregular development cycles. The project is experimental and intentionally low-level.

---

## Why This Exists

Modern systems hide the machine behind layers of abstraction. This project removes those layers completely.

The goal is not convenience. The goal is understanding:

- How interrupts actually work
- How paging behaves
- How context switching happens
- How hardware is programmed directly
- How operating systems function beneath modern tooling
- How the CPU executes machine code, cycle by cycle

---

Best regards,

**Michael Nordstedt**

michael@nordstedt.eu
