---
paths:
 - "kernel/src/includes/syscall.inc"
 - "commands/**"
 - "docs/syscalls.md"
---

# System Call ABI — Rules

**Last updated**: 2026-06-04

Applies to: changes in `kernel/src/includes/syscall.inc` and the syscall table in [docs/syscalls.md](../docs/syscalls.md). Read [docs/syscalls.md](../docs/syscalls.md) first — it is the canonical reference for the existing table.

## ABI (Immutable)

- Dispatch: `int 0x80`
- Args: `ebx`, `ecx`, `edx`, `esi`, `edi` (in that order; extras undefined)
- Return: `eax`; bad syscall number returns `-1`
- Gate: interrupt gate, DPL=3 in the IDT (syscalls run with **interrupts disabled** — IF is cleared on entry; this is not a typo, the PIT cannot preempt a syscall)

These are the values that existing userland programs are compiled against. **Do not change them.**

## Adding a New Syscall

1. **Pick a number.** The current highest is `sys_rmdir` at 21. The next free number is 22. Do not reuse numbers, do not renumber.
2. **Add the handler** in `kernel/src/includes/syscall.inc`:
   - Function header: `; in: ebx = ..., ecx = ..., ...; out: eax = return value`
   - Validate **every** pointer and length argument at the kernel boundary
   - Return 0 on success, -1 on error (matches the existing convention)
   - State any side effects in a comment (e.g. "modifies `current_task`")
3. **Add the dispatch case** in the `int 0x80` handler. Match the existing style (a `cmp`/`je` chain or a jump table — do not introduce a third style in the same file).
4. **Update [docs/syscalls.md](../docs/syscalls.md)** with the new number, args, returns, and a one-line description. Bump the "Last updated" stamp.
5. **Write a test caller** under `commands/<name>_test.asm` that exercises:
   - the success path with valid args
   - the failure path with each invalid arg (null pointer, negative length, out-of-range fd, etc.)
   - the "bad number" return (`int 0x80` with an unused number should return -1, not crash)
6. **Run the full validation gate** (see below).

## Argument Validation (Mandatory)

At the kernel boundary, **validate first**. The kernel is ring 0 and shares memory with userspace; a bad pointer from userspace will triple-fault the whole machine.

For pointer args (`esi`, `edi`):
- Check the address is within the program's loaded range
- If reading: ensure `[ptr .. ptr+len-1]` is fully mapped and readable
- If writing: ensure the range is mapped and writable
- If the program is short and the pointer is past its end, return -1

For length args (`ecx`, `edx`):
- Check non-negative
- Check does not exceed the program's loaded range
- For filesystem: check the resulting content fits the on-disk record size (1024 bytes per slot)

For file descriptor / index args (`ebx`):
- Check the index is within the open-file table or the directory entry count
- Return -1 if out of range

For path args (e.g. `chdir`, `unlink`, `create`):
- Resolve via the existing VFS path resolver
- Reject paths that escape the FS (no `..` traversal above the VFS root)
- Reject paths with embedded NUL bytes if length is also given

## When to Add a New Syscall (vs. fixing an existing one)

Add a new syscall when:

- The kernel needs to expose a new capability (e.g. a new graphics primitive, a new FS operation)
- The new operation cannot be expressed as a composition of existing syscalls
- The user-visible cost (one more `int 0x80`) is acceptable

Do **not** add a new syscall when:

- The change is just a fix or extension to an existing syscall's behaviour
- The new operation is a thin wrapper that could live in userspace (e.g. parsing a date format — parse it in the program, not the kernel)
- It exists only to make one specific program shorter

## Anti-Patterns (Don't)

- Don't renumber existing syscalls. `cat` is syscall 1 today and will be syscall 1 tomorrow.
- Don't change arg order on an existing syscall.
- Don't add a syscall that ignores pointer validation "for performance" — it will be the next regression.
- Don't use trap gate instead of interrupt gate "to allow nested IRQs during syscalls" — the current gate is intentional, and the `disk_busy` regression showed the cost of changing it (see `.clinerules/ai-context/handoffs.jsonl` for the post-mortem).
- Don't return a partial result in `eax` and a status in another register — return 0/-1, with output in caller-allocated buffers.

## Validation Gate

Before a syscall change ships:

1. `./asm` — clean assemble
2. `./asm -r` — boot, prompt appears
3. From the shell prompt, run your test caller:
   - success path: confirm `eax` is 0 and side effects match
   - each failure path: confirm `eax` is -1 and no kernel panic
   - bad syscall number: confirm `eax` is -1
4. Run `commands/cat` and `commands/ping` to confirm you did not break the existing dispatch (regression in `int 0x80` would take the whole shell down)
5. If the syscall touches paging (e.g. `mmap`-like): also run `./asm -d`, set a breakpoint at the syscall entry, and step through the validation
6. Update [docs/syscalls.md](../docs/syscalls.md) **in the same commit**

## Reference

- [docs/syscalls.md](../docs/syscalls.md) — full syscall table
- [docs/abi-contract.md](../docs/abi-contract.md) — userland binary contract
- [docs/architecture.md](../docs/architecture.md#subsystems) — syscall path within the kernel
- `.clinerules/ai-context/handoffs.jsonl` — past syscall regressions and their post-mortems

---

**Last updated**: 2026-06-04
