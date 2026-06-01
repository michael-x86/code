# Userland

## How It Works

Userland programs are flat 32-bit binaries assembled separately from the kernel.
They are embedded in `/bin` at build time and loaded on demand when you type their
name at the shell prompt. All interaction with the kernel happens through
`int 0x80` syscalls.

## Writing a Program

1. Create `commands/<name>.asm`
2. Start with `[bits 32]` and `[org 0x00000000]`
3. Use `int 0x80` for all kernel services
4. End with `ret` (returns to shell)
5. Append `<name>` to the `COMMANDS=(...)` array in the `asm` script
6. Run `./asm` to build

### Template

```nasm
[bits 32]
[org 0x00000000]

section .text
    ; your code here
    ; use int 0x80 syscalls

    ret
```

### Size Limit

Programs are capped at 4 KB. Use `resb` for zero-filled buffers at fixed
virtual addresses if you need more space.

## Included Programs

| Program   | Description                                      |
|-----------|--------------------------------------------------|
| `pwd`     | Print working directory                          |
| `ls`      | List current directory                           |
| `cd`      | Change directory (absolute, `.`, `..`, relative) |
| `cat`     | Print file contents                              |
| `touch`   | Create an empty file                             |
| `write`   | Write text to a file (args joined with spaces)   |
| `rm`      | Remove a file                                    |
| `mkdir`   | Create an empty directory                        |
| `rmdir`   | Remove an empty directory                        |
| `cp`      | Copy a file                                      |
| `mv`      | Move/rename a file                               |
| `vi`      | Minimal modal editor (hjkl, i, ESC, x, w, q)     |
| `ping`    | int 0x80 liveness test                           |
| `alloc`   | Allocate 4 KB pages                              |
| `dealloc` | Free previously allocated pages                  |
| `peek`    | Read a byte from an address                      |
| `poke`    | Write a byte to an address                       |
| `dump`    | Dump 16 bytes from an address                    |
| `ps`      | List running tasks                               |
| `exit`    | Shutdown                                         |
| `help`    | Print usage info                                 |
| `calc`    | Simple calculator                                |
| `argtest` | Print received arguments                         |

## Calling Convention

See [System Calls](syscalls.md) for the full `int 0x80` reference.

Key points:
- `eax` = syscall number
- Args in `ebx`, `ecx`, `edx`, `esi`, `edi`
- Return in `eax`
- Syscalls run with interrupts off — the PIT cannot preempt them
