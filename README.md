If you are looking for clean APIs and high-level comfort, this is not it.

If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.
# x86 Assembly Kernel

A small 32-bit operating system written entirely in NASM assembly. Boots from BIOS, switches to protected mode with paging, gates on a login prompt, and gives you a green-on-black shell over a round-robin process scheduler and an in-kernel filesystem you can edit and persist to disk. It can also flip the display into 800x400 VBE graphics (rendering a Mandelbrot set) or classic VGA mode 13h.

Built from scratch on Linux with no libc, no runtime, and no external abstractions.

```
Login: root
Password: ********
$ ls
bin  proc  var  etc
$ cat /proc/cpuinfo
processor      : 0
vendor_id      : Cyberdyne Systems
cpu family     : Neural-Net Processor
model          : T-800 Series 101
model family   : Skynet
stepping       : Version 2.4
flags          : learning infiltration phased-plasma
```

## Features

- x86 boot sector → 32-bit protected mode → paging → higher-half kernel at `0xC0100000`
- USB-bootable under a legacy BIOS / CSM — loads via LBA (`int 13h AH=42h`), one sector per call, no CHS geometry guessing
- GRUB-loadable via a Multiboot v1 header — `kernel.bin` can be dropped straight into a GRUB menu (no `bootloader.asm`)
- Login gate (`root` / `deadbeef`) before the shell starts
- Round-robin process table (8 slots) scheduled by IRQ0 (100 Hz PIT); background with `&`, list with `ps`, terminate with `kill`
- VGA text shell at 80×25 with in-line editing, command history, and tab completion
- Read/write in-kernel filesystem — `touch`, `write`, `rm`, `mkdir`, `cp`, `mv` from the prompt
- Persistence to disk — runtime files survive reboots **and** kernel rebuilds
- Linux-style `int 0x80` syscall interface (50 syscalls)
- Userland binaries assembled into `/bin` and spawned on demand
- Graphics: **ESC** toggles an 800x400 32bpp VBE mode (renders a colorful Mandelbrot set); `mode13` enters VGA mode 13h (320x200x256) with `setpixel` / `clearpixel` / `clear`, **F5** returns to text

## Quick start

Requires `nasm`, `python3`, `qemu-system-i386`.

```bash
make run         # build + run in a QEMU window
make fullscreen  # fullscreen
make debug       # GDB server on localhost:1234, halts at start
make             # build only (os.img)
make grub-iso    # build a bootable GRUB rescue ISO (grub.iso)
make grub-run    # boot that ISO in QEMU (loads the kernel via Multiboot)
make clean       # remove build artifacts
```

Log in with `root` / `deadbeef`.

### Raw QEMU commands

`make run` / `make grub-run` wrap these — use them directly when you want to tweak flags:

```bash
# your own bootloader
qemu-system-i386 -drive format=raw,file=os.img,index=0,if=ide -m 128

# via GRUB (Multiboot), os.img attached as the disk for persistence
qemu-system-i386 -cdrom grub.iso -boot d \
                 -drive format=raw,file=os.img,index=0,if=ide -m 128

# via QEMU's built-in Multiboot loader (no GRUB, no bootloader)
qemu-system-i386 -kernel kernel.bin \
                 -drive format=raw,file=os.img,index=0,if=ide -m 128
```

## Boot it from a USB stick

Write the whole image to the raw device (not a partition), and boot with **Legacy / CSM** enabled — it is a BIOS boot sector, not UEFI:

```bash
sudo dd if=os.img of=/dev/sdX bs=1M conv=fsync status=progress   # /dev/sdX = your stick — double-check it
```

The bootloader uses the BIOS-provided boot drive and LBA extended reads, and verifies `int 13h` extension support before loading (printing a clear error if the firmware lacks it).

## Boot it from GRUB (Multiboot)

<<<<<<< HEAD
The kernel carries a Multiboot v1 header, so GRUB can load `kernel.bin` directly — `bootloader.asm` is bypassed entirely, and the kernel installs its own GDT on entry (GRUB's segment selectors differ from the bootloader's). `entry_addr` matches the load address the bootloader uses, so **both boot paths keep working from the same binary**.
=======
## Add it to your machine's GRUB menu:
>>>>>>> ac9c2e173b1fc3ebd6977ee9be71dbb86cc48fd0

Test it locally with a rescue ISO (needs `grub-mkrescue`, plus `xorriso`, `mtools`, and `grub-pc-bin`):

```bash
make grub-run     # builds grub.iso and boots it in QEMU (SeaBIOS)
```

To add it to your real machine's GRUB menu — copy `kernel.bin` to `/boot/mykernel.bin`, add this to `/etc/grub.d/40_custom`, then `sudo update-grub`:

```
menuentry "Hobby OS" {
    multiboot /boot/mykernel.bin
    boot
}
```

Works on a **BIOS** boot, or **UEFI with CSM / legacy enabled**. On *pure* UEFI, GRUB loads the kernel fine but nothing displays — this is a VGA-text OS (it writes to `0xB8000`), and UEFI has no VGA text console. Note also that persistence writes to LBA 512+ of whatever disk it boots from, so file persistence only behaves as designed when `os.img` itself is the disk.

## Architecture at a glance

- **Bootloader** (`bootloader.asm`) — enables A20, checks for `int 13h` LBA support, loads the kernel one sector at a time into a buffer, enters protected mode, relocates the kernel to `0x00100000`, and jumps to it.
- **Kernel** (`kernel.asm`) — paging identity-maps the first 4 MB **and** maps it at `0xC0000000+`. Installs IDT/PIC/PIT/keyboard/syscall handlers, loads the persisted filesystem, runs the login gate, then schedules processes; slot 2 is the shell.
- **Filesystem** (`fs.inc`, auto-generated by `gen_fs.py`) — embedded as fixed-size 68-byte records inside the kernel image, each non-dir entry backed by a 2048-byte mutable buffer. 32 "spare slots" for runtime-created files.
- **Persistence** — modified spare slots are written through to a fixed region on the disk image (starting at LBA 512, 5 sectors per slot). A boot-time loader replays them into RAM.
- **Userland** — programs are assembled separately into ≤4 KB flat binaries (`[org 0x00000000]`), embedded in `/bin` at build time, and spawned into a process slot when invoked. They talk to the kernel only via `int 0x80`.

## Layout

```
.
├── bootloader.asm     # 16→32 bit boot stub (A20, LBA load, protected mode)
├── kernel.asm         # the kernel
├── gen_fs.py          # walks build dir, emits fs.inc
├── Makefile           # build entry point
├── commands/          # userland program sources (*.asm)
├── bin/               # compiled userland (no extension)
├── proc/  var/log/    # host-side content mirrored into the FS
```

Whatever sits in `bin/`, `proc/`, `var/log/` on the host shows up at the matching path inside the OS. Rebuilding picks up changes automatically.

## Adding a program

1. Write `commands/<name>.asm` starting with `[bits 32]` / `[org 0x00000000]` and ending in `ret`.
2. Use only `int 0x80` to talk to the kernel.
3. Append `<name>` to the `COMMANDS` list in the `Makefile`.
4. `make`. The binary lands at `/bin/<name>` and is callable by typing `<name>` at the prompt.

Programs are capped at 4 KB. Read arguments with `sys_get_arg` (syscall 14); use the position-independent `call .base / pop ebp / sub ebp,.base` idiom for local data (see `commands/kill.asm`).

## int 0x80 syscalls

`eax` = syscall number, args in `ebx / ecx / edx / esi / edi`, return in `eax`. Bad number → `-1`. There are 50 syscalls spanning:

- **Console** — print, print_cr, newline, cls, print_hex/int, putchar, banner
- **Input / time** — get_key, get_tick, hertz, tick, epoch
- **Filesystem** — getcwd, chdir, list_dir, stat, create, write, unlink, mkdir, rmdir
- **Memory** — read_mem, alloc, dealloc, peek, poke, mem_dump, heap_info
- **Processes** — ps_info, kill, detach (self-background), exit
- **Graphics** — mode13, setpixel (`ebx=x ecx=y edx=colour`), clearpixel (`ebx=x ecx=y`), gclear
- **Misc** — get_arg, hex2int, asc2int, setfreq (PIT rate), reg_dump, stack_dump

Syscalls run with interrupts off (interrupt gate), so the PIT cannot preempt them.

## Userland programs (`/bin/`)

`pwd ls cd cat touch write rm rmdir mkdir cp mv` (files) ·
`ps kill` (processes) · `alloc dealloc heap` (memory) ·
`peek poke memdump regdump stackdump bin2hex` (inspection) ·
`mode13 setpixel clearpixel clear plot bounce dydx up` (graphics/demos) ·
`frequency epoch banner help cls verify exit` ·
`run` (batch: execute each line of `exec.cmd`; unknown lines print `command not found`)

`write` overwrites a file with one line; `write -a <file> <text>` appends a line, so you can build a multi-line `exec.cmd`:

```
touch exec.cmd
write exec.cmd pwd          # first line (overwrite)
write -a exec.cmd epoch     # append a line
write -a exec.cmd ls        # append another
run                         # runs pwd, epoch, ls in order
```

## How persistence works ;-)

Each runtime-created file lives in two places: a 2048-byte buffer in RAM, and a fixed slot on disk (LBA 512 + slot × 5 sectors). The mapping never moves. On `create` / `write` / `unlink`, the kernel updates RAM and writes that slot's sectors to disk. On boot, `load_fs_persist` reads each slot back and replays it into RAM.

<<<<<<< HEAD
Build-seeded files (everything under `/proc`, `/etc`, `/var/log`) live inside the kernel image and reset to their build-time content on every boot — only the 32 spare slots persist. The Makefile backs up the FS region before reassembling the kernel and restores it after, so persisted files survive rebuilds too.
=======
- eax = syscall number
- ebx/ecx/edx/esi/edi = arguments
- eax = return value

# Available syscall groups:

- Console output
- Keyboard and timing
- Filesystem operations
- Memory management
- Process control
- Graphics
- Debugging tools

Syscalls execute through interrupt gates, 

with interrupts disabled during handling.

## Included programs

# Filesystem

pwd ls cd cat touch write rm rmdir mkdir cp mv

# Processes

ps kill

# Memory / Debug

alloc dealloc heap
peek poke memdump regdump stackdump bin2hex

# Graphics / Demos

mode13 setpixel clearpixel clear plot bounce dydx up

# Utilities

frequency epoch banner help cls verify exit

- Everything else is a bug waiting for a debugger.

## Persistence

Runtime-created files are stored in:

- RAM (active filesystem state)
- Fixed disk slots (persistent storage)

Each slot has a permanent location, so files survive:

- Reboots
- Kernel rebuilds
- Minor chaos

Build-time files (`/proc`, `/etc`, `/var/log`) 
are embedded into the kernel image and reset on rebuild. 
Runtime-created files use the persistent storage area.

The build system preserves the filesystem region while rebuilding, 
so your carefully created files do not disappear into the void.
>>>>>>> ac9c2e173b1fc3ebd6977ee9be71dbb86cc48fd0

## Graphics modes

- **800x400 VBE** — press **ESC** to toggle between text and a 32bpp linear-framebuffer graphics mode. On entry it renders a **colorful Mandelbrot set** (fixed-point Q16.16 math, coloured from the `mandel_colors` palette; the set interior is black). Explore it live: **arrow keys** pan, **keypad +/−** zoom in/out, **R** resets the view. Edit the `mandel_colors` `dd` table in `kernel.asm` to restyle the palette, or `MAND_ITER` for more iteration depth.
- **VGA mode 13h (320x200x256)** — run `mode13`. The shell keeps running (typed blind over the graphics screen), so you can `setpixel <x> <y> <colour>`, `clearpixel <x> <y>`, and `clear`. Press **F5** to return to text. Entering saves the font and DAC palette; exiting restores both, so text colours come back intact. Colours are RGB332 palette indices (`0xE0` red, `0x1C` green, `0x03` blue, `0xFF` white).

## Debugging with GDB

```bash
make debug
gdb
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) continue
```

No debug symbols (flat binary). Use raw addresses: `0x7C00` (bootloader), `0xC0100000` (kernel).

## Known limitations

- Ring 0 only — no userspace memory protection. A buggy program can corrupt the kernel.
- The login password is stored in plaintext in the kernel image — a gate, not a security boundary.
- Path resolver handles absolute paths, `.`, `..`, and a single relative name. `cd foo/bar` is not supported.
- File content capped at 2048 bytes (`FS_CAPACITY`); 32 runtime-creatable files (`FS_SPARE_COUNT`).
- No interrupt-driven I/O — the keyboard is IRQ-fed into a ring buffer; disk is PIO.

---

# Design Philosophy

## Zero Abstraction

If it is not explicitly written, it does not exist.

## Instruction-Level Control

Every register, flag, interrupt frame, and memory mapping might be intentional.

## Hardware-First Engineering

The kernel is designed around CPU behavior and hardware constraints rather than high-level software conventions.

---

# Why This Exists

Modern systems hide the machine behind layers of abstraction. This project removes those layers completely.

The goal is not convenience. The goal is understanding:

- how interrupts actually work
- how paging behaves
- how context switching happens
- how hardware is programmed directly
- how operating systems function beneath modern tooling
