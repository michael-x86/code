# Userland ABI Contract

Every program in `commands/` must obey the same binary contract. The kernel
loader (`kernel/exec.inc`) places each program at an **arbitrary virtual
address** chosen by the page allocator. The program itself is responsible for
computing its runtime base address and converting all of its data references
into base-relative form.

This page is the spec. Read it before writing or modifying a userland program.

---

## Why this exists

The loader calls the program like this:

```nasm
mov eax, [exec_vbase]    ; chosen by find_free_virt()
call eax                 ; _start runs at this vaddr
```

`exec_vbase` is whatever free virtual range the page allocator hands out at
the time. It is not 0, it is not constant, and it changes between runs and
between invocations.

If the program contains an instruction like `mov esi, msg` and `msg` lives at
file offset `0x20`, NASM will encode it as `mov esi, 0x20`. At runtime, `esi`
becomes `0x20` — which on this kernel is the start of the IDT. The program
then reads the IDT bytes and prints them as a string, hangs, or triple-faults.

The contract below turns every reference into `lea esi, [ebp + 0x20]` so the
address is recomputed at runtime from the program's actual base.

---

## The four rules

### Rule 1 — anchor ebp on entry

The first thing `_start` must do is compute its own base address into `ebp`:

```nasm
_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base
    ; ... rest of program ...
    ret
```

Mechanism:
1. `call .get_base` pushes the address of the next instruction onto the stack.
2. `pop ebp` pulls that address into `ebp`.
3. `sub ebp, .get_base` subtracts the file offset of the `.get_base` label
   (always 5 in the standard pattern) so that `ebp` becomes the program's
   load address, not the return address of the call.

After this prologue, `ebp` is the program's vaddr base for the entire
lifetime of the run. Treat it as read-only.

### Rule 2 — never use absolute data references

Every reference to a label in `.text` or `.rodata` must go through `ebp`.

| Forbidden | Required |
|---|---|
| `mov esi, msg` | `lea esi, [ebp + msg]` |
| `mov edi, buf` | `lea edi, [ebp + buf]` |
| `mov esi, [info]` | `mov esi, [ebp + info]` |
| `mov [stat_buf], eax` | `mov [ebp + stat_buf], eax` |
| `cmp dword [info], 0` | `cmp dword [ebp + info], 0` |
| `mov esi, [info + 4]` | `mov esi, [ebp + info + 4]` |

Numeric immediates are fine. `mov esi, 1` and `mov eax, 26` stay as-is — only
label references need the indirection.

### Rule 3 — buffers go in the file, not in `section .bss`

The loader copies `file_size` bytes from disk into the first mapped page and
**does not zero the rest of the page**. Anything in `section .bss` lands in
uninitialized memory (whatever the page allocator previously had in that
frame) and is not safe to use.

The proven pattern, used by every working program:

```nasm
[bits 32]
[org 0x00000000]

; ... .text ...
; ... .rodata: usage_msg, err_msg, etc. ...

align 4
arg:    times 128 db 0   ; in-file zero buffer, last 128 bytes of the binary
info:   times 12  db 0
```

These trailing `times N db 0` blocks live in `.text` (no `section .bss`
directive). They become part of the file on disk, so the loader reads them as
zeros into the mapped page.

The program must compute the buffer offset from `ebp` the same way it
computes any other label — `lea edi, [ebp + arg]`.

### Rule 4 — return to the caller

The loader calls the program with `call eax`, so the return address is on the
stack. End the program with `ret`. Do not call `sys_shutdown` (that's
`#9` — kills the whole machine) and do not call into kernel functions
directly.

---

## Working template

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

    ; --- read first argument ---
    mov ebx, 1
    lea edi, [ebp + arg]
    mov eax, 14              ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage

    ; --- do the work ---
    lea esi, [ebp + arg]
    mov eax, 1              ; sys_print
    int 0x80
    ret

.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

usage_msg db "usage: mycmd <arg>", 13, 0

align 4
arg: times 128 db 0
```

---

## Common mistakes

### Hardcoded vaddr

```nasm
%define arg 0xC0700400     ; WRONG — only works at one specific vaddr
mov edi, arg
```

The kernel's vaddr layout changes when you add heap, paging, or a frame
buffer. Never hardcode a kernel-side address in a userland program.

### Absolute `mov` of a label

```nasm
mov esi, msg              ; WRONG — encodes msg's file offset as a vaddr
lea esi, [ebp + msg]      ; right
```

### Using `section .bss`

```nasm
section .bss              ; WRONG — loader does not zero extra page bytes
arg: resb 128
```

Either put the buffer in `.text` as `times N db 0` (the contract), or use
`resb` only if you have arranged for the loader to zero the region (we have
not).

### Anchor present but unused

```nasm
_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base     ; sets up ebp ...
    mov esi, msg           ; ... and then ignores it — WRONG
```

The anchor is not enough by itself. Every data reference in the body must go
through `ebp`.

### Returning with `int 0x80` to sys_shutdown

```nasm
mov eax, 9
int 0x80
ret                        ; never reached; the whole kernel just died
```

Use `ret` for a clean exit. Reserve `sys_shutdown` for the `exit` command.

---

## Verifying a new program

After writing a program, run these checks before adding it to `COMMANDS=()`:

1. **Disassemble it and look at every data reference:**

   ```
   ndisasm -b 32 -o 0x00000000 build/bin/<name>
   ```

   Any `mov esi|edi|ebx, <small constant>` near a known data label is a bug.
   The correct form is `lea esi|edi|ebx, [ebp + <small constant>]`.

2. **Grep the source for the forbidden patterns:**

   ```
   grep -nE "mov\s+(esi|edi|ebx),\s+[a-zA-Z_]" commands/<name>.asm
   grep -nE "\[\s*[a-zA-Z_][a-zA-Z0-9_]*\s*(\+[0-9]+)?\s*\]" commands/<name>.asm
   ```

   The first catches `mov reg, label`. The second catches `[label]`. Both
   should produce zero hits (constants and offsets from `ebp` are fine).

3. **Build and run the kernel, then invoke the command at the shell prompt.**

---

## Loader reference (for context)

`kernel/exec.inc` does the following for each program:

1. Look up `/bin/<argv[0]>` via `fs_lookup_inode`.
2. Read the file into `file_block_buf` via `file_read`.
3. Call `find_free_virt` for `page_count` pages.
4. `alloc_page` + `map_page` for each page.
5. `rep movsb` the file content into the mapped region.
6. Save `exec_vbase` / `exec_pages` for the post-return cleanup.
7. `call [exec_vbase]`.
8. After `ret`, `free_pages` and clear the `alloc_table` entry.

The program gets pages with the file content at offsets `[0 .. file_size)` and
arbitrary uninitialized bytes at offsets `[file_size .. pages*4096)`. There
is no relocation, no dynamic linker, and no stack setup beyond the call
frame the loader pushes.
