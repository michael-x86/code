# Contributing to x86 Kernel

Thanks for your interest in contributing! This is a low-level, instruction-centric x86 kernel project. Contributions are welcome — whether fixes, optimizations, new syscalls, or documentation.

## Getting Started

1. **Fork** the repository
2. **Clone** your fork: `git clone https://github.com/michael-x86/code.git`
3. **Create a branch** for your change: `git checkout -b feature/my-feature` or `git checkout -b fix/my-bug`
4. **Make your changes**
5. **Test thoroughly** (see below)
6. **Push** to your fork and open a pull request

## Development Setup

You'll need:
- `nasm` — NASM assembler
- `python3` — for filesystem generation (`gen_fs.py`)
- `qemu-system-i386` — to run the kernel

## Building and Testing

```bash
# Build and run in QEMU
./asm -r

# Build only (no run)
./asm

# Run with GDB server on localhost:1234
./asm -d

# Fullscreen mode
./asm -f
```

**Every change must be tested with `./asm -r`** to ensure the kernel still boots and behaves as expected.

## Code Style Guide

### Assembly (NASM)

- **Labels:** lowercase with underscores (e.g., `setup_paging`, `.check_loop`)
- **Constants:** UPPERCASE with underscores (e.g., `CODE_SEG`, `DATA_SEG`)
- **Registers:** Use meaningful register names in comments
- **Comments:** 
  - Document non-obvious logic
  - Explain hardware assumptions and side effects
  - Mark sections clearly with `; --- Section Name ---`
- **Indentation:** Use spaces (match surrounding code)
- **Function structure:**
  ```asm
  ; --- Brief description of what this does ---
  ; in:  eax = description
  ;      ebx = description
  ; out: eax = description
  ;      CF = 1 on failure, 0 on success
  my_function:
      push ebx
      ; ... code ...
      pop ebx
      ret
  ```

### Python (gen_fs.py)

- Follow PEP 8
- Comment non-obvious filesystem serialization logic
- Document any changes to the 68-byte record format

### Commit Messages

- **First line:** imperative mood, ≤50 characters
  - ✅ `Fix paging setup for >20MB kernels`
  - ✅ `Add sys_read syscall`
  - ❌ `Fixed stuff`
- **Body:** explain *why*, not *what* (the code shows what)
- **Reference issues:** `Fixes #42`

Example:
```
Add higher-half kernel mapping for task isolation

Previously all tasks shared the lower 4 MB. This change adds
separate page table entries at 0xC0400000+ to support future
ring-3 userspace with separate memory contexts.

Fixes #18
```

## Types of Contributions

### Bug Fixes
- Are they reproducible? Include steps
- Does the fix break anything else? Test thoroughly
- Document the root cause in the PR

### New Syscalls
- **Update the README table** (`int 0x80 syscall table`)
- **Add kernel handler** in `kernel.asm` (with `in:/out:` comments)
- **Add test program** in `commands/` if user-visible
- **Update CONTRIBUTING.md** if changing the calling convention

### New Commands
- Create a new file in `commands/` following the template
- Add the command name to `COMMANDS=()` in the `asm` script
- Size limit: **4 KB** (use `resb` for larger buffers at fixed vaddrs)
- Only use `int 0x80` to interact with the kernel
- Update README with usage and syscall list

### Documentation
- Fixes to README or CONTRIBUTING are always welcome
- Explain architecture decisions (not just syntax)
- Include examples where helpful

## Pull Request Checklist

Before submitting:
- [ ] Code tested with `./asm -r` (kernel boots, basic functionality works)
- [ ] New assembly includes `in:` / `out:` comments for functions
- [ ] Commit messages follow the style guide
- [ ] No unrelated changes mixed in
- [ ] README/CONTRIBUTING updated if needed
- [ ] Explain *why* the change was needed (not just what changed)

## Questions?

Open an **issue** with the `question` label.

## License

All contributions are licensed under the MIT License (see `LICENSE`).

---

**Happy coding!** Remember: every register, flag, and interrupt is intentional. Comments that explain hardware behavior are worth their weight in silicon.
