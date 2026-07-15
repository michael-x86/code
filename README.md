If you are looking for clean APIs and high-level comfort, this is not it.

If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.
# x86 Assembly Kernel

A small 32-bit operating system written entirely in NASM assembly. Boots from BIOS, switches to protected mode with paging, gates on a login prompt, and gives you a green-on-black shell over a round-robin process scheduler and an in-kernel filesystem you can edit and persist to disk. It can also flip the display into 800x600 VBE graphics or classic VGA mode 13h.

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
`    

## Adding a program

1. Write `commands/<name>.asm` starting with `[bits 32]` / `[org 0x00000000]` and ending in `ret`.
2. Use only `int 0x80` to talk to the kernel.
3. Append `<name>` to the `COMMANDS` list in the `Makefile`.
4. `make`. The binary lands at `/bin/<name>` and is callable by typing `<name>` at the prompt.

Programs are capped at 4 KB. Read arguments with `sys_get_arg` (syscall 14); use the position-independent `call .base / pop ebp / sub ebp,.base` idiom for local data (see `commands/kill.asm`).
 on disk (LBA 512 + slot × 5 sectors). The mapping never moves. On `create` / `write` / `unlink`, the kernel updates RAM and writes that slot's sectors to disk. On boot, `load_fs_persist` reads each slot back and replays it into RAM.

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
