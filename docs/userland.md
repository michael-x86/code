# Userland

## How It Works

Userland programs are flat 32-bit binaries assembled separately from the kernel.
They are embedded in `/bin` at build time and loaded on demand when you type
their name at the shell prompt. All interaction with the kernel happens
through `int 0x80` syscalls.

The loader places every program at an **arbitrary virtual address** chosen by
the page allocator at run time. Programs must therefore follow a small ABI
contract to be position-independent. **Read [ABI Contract](abi-contract.md)
before writing a new program** — the rules are short, the failure modes are
not.

## Writing a Program

1. Create `commands/<name>.asm`
2. Start with `[bits 32]` and `[org 0x00000000]`
3. Follow the [ABI contract](abi-contract.md) — anchor `ebp`, use
   `lea reg, [ebp + label]` for all data references, end with `ret`
4. Use `int 0x80` syscalls (see [System Calls](syscalls.md))
5. Append `<name>` to the `COMMANDS=(...)` array in the `asm` build script
6. Run `./asm` to build

### Template

The full, contract-compliant template:

```nasm
; mycmd - one-line description
;
; ABI contract: see docs/abi-contract.md
[bits 32]
[org 0x00000000]


_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; ... your code here; use [ebp + label] for all data refs ...

    ret

; --- read-only data ---
usage_msg db "usage: mycmd", 13, 0

; --- in-file zero buffers (NOT section .bss — see ABI contract) ---
align 4
arg: times 128 db 0
```

### Size Limit

Programs are capped at 4 KB (one page). If you need more working space,
allocate at run time with `sys_alloc_pages` (`int 0x80` with `eax = 23`,
`ecx` = byte count) — the returned vaddr is yours to keep for the duration
of the program and is freed automatically when you `ret`.

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
