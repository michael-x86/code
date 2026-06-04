---
paths:
 - "commands/**"
---

# Userland Programs — Rules

**Last updated**: 2026-06-04

Applies to: `commands/*.asm`. Read [docs/userland.md](../docs/userland.md) and [docs/abi-contract.md](../docs/abi-contract.md) before writing or modifying a userland program.

## The Userland Contract

Userland programs are **flat 32-bit binaries** assembled separately from the kernel, embedded in `/bin` at build time, and loaded on demand when you type the program name at the shell prompt. The kernel loader calls your program like this:

```nasm
mov eax, [exec_vbase]    ; chosen by find_free_virt() — not 0, not constant
call eax                 ; _start runs at this vaddr
```

`exec_vbase` is whatever free virtual range the page allocator hands out at the time of the call. It is **not 0, it is not constant, and it changes between runs**. If you ignore this, your program reads garbage from the IDT and either prints nonsense or triple-faults.

**Read [docs/abi-contract.md](../docs/abi-contract.md) before writing a new program.** The rules are short, the failure modes are not.

## The Three Rules

1. **Anchor `ebp`** in your `_start` and use `lea reg, [ebp + label]` for every data reference. This is how you survive an arbitrary load address.
2. **No absolute addresses** in your source — NASM will encode them as file offsets, and at runtime those offsets point into the IDT.
3. **End with `ret`** — the kernel `call`s your program; you return to the loader.

## Required Program Header

```nasm
[bits 32]
[org 0x00000000]

; --- program name ---
; purpose: one-line description
; syscalls used: 0 (putchar), 1 (print), ...
; ---
_start:
    mov ebp, 0xDEADBEEF      ; placeholder — overwritten by loader
    ; ... program body ...
    ret
```

The `[org 0x00000000]` is required for NASM to assemble position-relative references correctly. The loader overwrites the `mov ebp, 0xDEADBEEF` placeholder with the real base address (see `kernel/exec.inc`, search for `exec_bin` / `exec_vbase` patching). The file-offsets written to the binary are preserved, so the patched `ebp` makes every `lea reg, [ebp + label]` resolve to the correct runtime address.

## Style

Follow [docs/contributing.md](../docs/contributing.md) and the existing programs in `commands/`. Specifically:

- `; --- Section Name ---` headers, blank lines around
- Function headers with `; in:` / `; out:`
- Lowercase underscore labels (`.local` for locals)
- UPPERCASE for `EQU` constants
- No `extern` — you are a flat binary, you have no linker
- No NASM warnings. None.

## Syscalls

- Use only `int 0x80` to talk to the kernel
- Args in `ebx`, `ecx`, `edx`, `esi`, `edi` (in that order)
- Return value in `eax`; bad syscall number returns `-1`
- See [docs/syscalls.md](../docs/syscalls.md) for the full table and `.clinerules/syscalls.md` for rules on adding new ones

## Size Cap

Programs are capped at **64 KB** flat binary. NASM `-f bin` will happily emit a 4 MB file if you `resb` a big buffer — the `build/asm` script will reject anything over 65536 bytes and abort. If you need a big buffer, allocate it on the stack at runtime, not with `resb`.

## Adding a Program to the Build

After writing `commands/<name>.asm`:

1. Append `<name>` to the `COMMANDS=(...)` array in `build/asm` (alphabetic order with the others; do not edit `./asm`, it delegates to `build/asm`)
2. Run `./asm` — it should compile and place `build/bin/<name>`
3. Run `./asm -r` — boot, type `<name>` at the prompt, confirm it works
4. If the program touches the filesystem: also validate that the read/write paths match `docs/filesystem.md`

## ABI Quick-Reference (from docs/abi-contract.md)

- Anchor `ebp` to the loader-supplied base
- Use `lea reg, [ebp + label]` for **all** data references (strings, buffers, jump tables)
- Use `lea reg, [ebp + label + offset]` if you need a fixed offset from a label
- For self-modifying code or computed jumps: compute the target as `[ebp + label]` plus any constant
- For string constants that exceed `EQU`-able sizes, store them as `db` near a label, not as `dd <address>`

## Anti-Patterns (Don't)

- Don't use `mov reg, <label>` — that encodes the file offset, not the runtime address. Use `lea reg, [ebp + label]`.
- Don't `[org 0x10000]` or any other non-zero `[org]` — the loader does the relocation, not NASM.
- Don't use `times` to pad the binary to a fixed size — it inflates `build/bin/<name>` and may exceed the cap.
- Don't call kernel functions directly — go through `int 0x80`.
- Don't store pointers in segment registers.
- Don't assume `exec_vbase` is aligned to a page or to anything — the only contract is "use `ebp`".

## Validation Gate

Before any userland change ships:

1. `./asm` — clean assemble, no warnings, your program lands in `build/bin/<name>`
2. `./asm -r` — boot, invoke your program by typing `<name>` at the prompt
3. Exercise every code path the program has (don't just smoke-test)
4. If the program parses arguments: invoke with 0, 1, and many args
5. If the program reads/writes files: confirm the file exists after the call and persists across `./asm` rebuild
6. If the program uses a syscall for the first time: confirm the syscall number matches [docs/syscalls.md](../docs/syscalls.md) exactly

---

**Last updated**: 2026-06-04
