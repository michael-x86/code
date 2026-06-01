# Getting Started

## Dependencies

| Tool              | Required | Purpose                            |
|-------------------|----------|------------------------------------|
| `nasm`            | yes      | NASM assembler                     |
| `python3`         | yes      | filesystem generation (gen_fs.py)  |
| `qemu-system-i386`| yes     | run the kernel                     |
| `gdb`             | no       | needed for `./asm -d`              |

### Install

```
Debian/Ubuntu:  sudo apt install nasm qemu-system-x86 python3
Fedora:         sudo dnf install nasm qemu-system-x86 python3
Arch:           sudo pacman -S nasm qemu-system-x86 python3
macOS (brew):   brew install nasm qemu
```

## Setup

```
./setup.sh          # check deps, scaffold dirs, print next steps
./setup.sh --build  # setup + compile
./setup.sh --run    # setup + compile + launch QEMU
./setup.sh --debug  # setup + compile + launch with GDB
```

## Build & Run

```
./asm             # build only → build/os.img
./asm -r          # build + run in a QEMU window
./asm -f          # build + run fullscreen
./asm -d          # build + start GDB server on localhost:1234
```

## What You See

A green-on-black VGA shell (80x25). The prompt shows the current directory.
Type `help` for available shell commands, or try:

```
$ ls
$ cd /proc
$ cat cpuinfo
$ touch myfile
$ write myfile hello world
$ cat myfile
$ vi myfile
```

## Next

- [Architecture](architecture.md) — how it all fits together
- [Userland](userland.md) — writing your own programs
