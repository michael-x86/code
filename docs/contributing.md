# Contributing

## Getting Started

1. Fork the repository
2. Clone your fork
3. Create a branch: `git checkout b feature/name` or `git checkout b fix/name`
4. Make changes and test thoroughly
5. Push and open a pull request

Every change must be tested with ./asm r to verify the kernel boots.

## Code Style

### Assembly (NASM)

- Labels: lowercase with underscores (setup_paging, .check_loop)
- Constants: UPPERCASE with underscores (CODE_SEG, DATA_SEG)
- Comments on non-obvious logic, hardware assumptions, side effects
- Sections marked with ; --- Section Name ---
- Function header comments:

```
; --- Brief description ---
; in:  eax = input description
;      ebx = input description
; out: eax = return value
;      CF = 1 on failure
my_function:
    push ebx
    ; ...
    pop ebx
    ret
```

### Python (gen_fs.py)

Follow PEP 8. Document any changes to the 68-byte record format.

### Commit Messages

- First line: imperative mood, 50 chars max
- Body: explain why, not what
- Reference issues: Fixes #42

## Types of Contributions

### Bug Fixes
Include reproduction steps, test thoroughly, document root cause in PR.

### New Syscalls
- Update this documentation and the README table
- Add handler in kernel.asm with in:/out: comments
- Add test program in commands/ if user-visible

### New Commands
- Create commands/<name>.asm following the userland template
- **Read [docs/abi-contract.md](abi-contract.md) first** — every program
  must be position-independent
- Add to COMMANDS=() in the build script
- 4 KB size limit
- Only use int 0x80 for kernel interaction

## PR Checklist

- Code tested with ./asm r (kernel boots, basic functions work)
- New assembly includes in:/out: comments
- Commit messages follow the style guide
- No unrelated changes mixed in
- Documentation updated if needed

## License

MIT (see LICENSE).
