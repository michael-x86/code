# Developer Guide

## Project Overview

This is a from-scratch x86 OS with:
- Custom bootloader (kernel/bootloader.asm)
- Kernel with paging, VFS, syscalls, shell, userland
- Userland programs in `commands/` directory
- Build system using NASM assembler

---

## Build System

### Quick Start

```bash
cd /home/janko/dev/code

# Build everything (kernel + userland + disk images)
./build/asm

# Build and run in QEMU
./build/asm -r

# Build single program
./build.sh alloc
# or
make alloc
```

### Build Scripts

1. **`build/asm`** — Main build script
   - Compiles kernel, bootloader, all userland programs
   - Generates filesystem image (`fs.img`)
   - Creates bootable disk image (`os.img`)
   - Options: `-r` (run), `-f` (fullscreen), `-d` (debug)

2. **`build.sh`** — Alternative build script
   - Compiles single or all userland programs
   - Uses `-i` flag to set include path for `userland.inc`

3. **`Makefile`** — Make-based build
   - `make` — Build all programs
   - `make alloc` — Build single program
   - `make clean` — Clean build artifacts

---

## Userland Program Development

### Standard Structure

All userland programs now use the shared `userland.inc` library, which eliminates ~400 lines of duplicated boilerplate across 26 programs.

**Basic template:**

```asm
; program_name — description.  usage: program_name <args>
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START

    ; Your code here
    
    ret

; Data section
usage_msg: db "usage: program_name <args>", 13, 0

section .bss
alignb 4
arg_buf: resb 32
```

### Available Macros (from userland.inc)

#### Startup
- `USERLAND_START` — Position-independent startup boilerplate (replaces 3-line pattern)

#### Syscall Wrappers (inline)
- `SYS_GET_ARG` — Get argument (eax=14)
- `SYS_PRINT` — Print NUL-terminated string (eax=1)
- `SYS_PRINT_CR` — Print string with CR termination (eax=2)
- `SYS_NEWLINE` — Print newline (eax=3)
- `SYS_PRINT_HEX` — Print hex value in ebx (eax=5)
- `SYS_ASC2INT` — Parse decimal string to integer (eax=27)
- `SYS_STAT` — Get file status (eax=15)
- `SYS_PRINT_N` — Print N bytes (eax=16)

#### Error/Usage Handlers
- `PRINT_USAGE label` — Print usage message and return
- `PRINT_ERROR label` — Print error message and return
- `PRINT_SUCCESS label` — Print success message and return

#### Argument Parsing Helpers
- `GET_ARG index, buffer` — Get argument by index
- `PARSE_INT` — Parse string in esi as integer, result in ecx

#### BSS Template
- `SECTION_BSS_DEFAULT` — Standard 32-byte argument buffer

### Example: Reading Arguments

```asm
_start:
    USERLAND_START

    ; Get first argument (index 1)
    GET_ARG 1, arg_buf
    cmp eax, -1
    je .usage

    ; Parse as integer
    lea esi, [ebp + arg_buf]
    PARSE_INT
    test ecx, ecx
    jz .usage

    ; ecx now contains the parsed integer
    ; ... do something with it ...

    ret

.usage:
    PRINT_USAGE usage_msg
```

### Example: Printing Output

```asm
_start:
    USERLAND_START

    ; Print string
    lea esi, [ebp + msg]
    SYS_PRINT

    ; Print hex value
    mov ebx, eax        ; value to print
    SYS_PRINT_HEX

    ; Print newline
    SYS_NEWLINE

    ret

msg: db "Result: ", 0
```

---

## Kernel Development

### Key Include Files

**`kernel/src/includes/` directory:**
- `lib.inc` — Shared string/numeric functions (str_eq, str_len, asc2int, hex2int, print_hex_*, print_int_decimal)
- `vga.inc` — VGA text-mode driver (putchar, print, newline, cls, scroll, cursor)
- `memory.inc` — Page allocation (alloc_page, free_pages)
- `fs_core.inc` — Filesystem core operations
- `syscall.inc` — System call handlers
- `idt.inc` — Interrupt descriptor table
- `pic.inc` — Programmable interrupt controller
- `irq.inc` — Interrupt request handlers
- `shell.inc` — Shell implementation
- `commands.inc` — Built-in shell commands

### Adding a New Syscall

1. Define syscall number in `kernel/src/includes/constants.inc`
2. Implement handler in appropriate `.inc` file
3. Add to syscall dispatch table in `kernel/src/kernel.asm`
4. Document in `docs/syscalls.md`

---

## Refactoring History

### 2026-06-04: Userland Boilerplate Elimination

**Problem:** 26 userland programs had identical `_start` boilerplate and duplicated syscall wrapper code (~400+ lines total).

**Solution:** Created `lib/userland.inc` with macros for:
- Standardized `_start` sequence (`USERLAND_START`)
- Syscall wrappers (`SYS_GET_ARG`, `SYS_PRINT`, etc.)
- Error handling (`PRINT_USAGE`, `PRINT_ERROR`)
- Argument parsing helpers (`GET_ARG`, `PARSE_INT`)

**Impact:**
- Eliminated ~400+ lines of duplicated code
- Improved maintainability (change once in library, not 26 files)
- Enhanced readability (self-documenting macro names)
- All 26 programs refactored and verified working

**Files modified:**
- Created: `lib/userland.inc`, `build.sh`, `Makefile`
- Modified: `build/asm` (added `-i` flag for include path)
- Refactored: All 26 programs in `commands/`

See `docs/duplicate-code-analysis.md` and `docs/refactoring-summary.md` for details.

---

## Debugging

### QEMU Debugging

```bash
# Run with GDB stub
./build/asm -d

# In another terminal:
gdb -ex "target remote localhost:1234" kernel/kernel.bin
```

### Common Issues

1. **"unable to open include file" error**
   - Ensure you're using `./build/asm` or `./build.sh` (they set include path)
   - Don't run `nasm` directly without `-i lib/` flag

2. **Program compiles but fails at runtime**
   - Check that `userland.inc` is included
   - Verify `_start:` is the first code label (not functions before it)
   - Use `nasm -E` to see macro expansion

3. **Build script not finding programs**
   - Check `commands/` directory for `.asm` files
   - Verify program name in `build/asm` COMMANDS array

---

## File Structure

```
/home/janko/dev/code/
├── kernel/
│   ├── bootloader.asm          # Bootloader source
│   ├── kernel.bin              # Compiled kernel
│   ├── src/
│   │   ├── kernel.asm         # Kernel entry point
│   │   └── includes/         # Kernel include files
│   └── disk.img
├── commands/                   # Userland programs (.asm)
├── lib/
│   └── userland.inc          # Shared userland library
├── build/
│   ├── asm                   # Main build script
│   ├── bin/                 # Compiled userland programs
│   ├── os.img               # Complete disk image
│   ├── boot.img             # Bootable image (bootloader + kernel)
│   └── fs.img               # Filesystem image
├── docs/                     # Documentation
│   ├── duplicate-code-analysis.md
│   ├── refactoring-summary.md
│   └── DEVELOPER.md         # This file
├── build.sh                  # Alternative build script
├── Makefile                  # Make-based build
└── scripts/                 # Utility scripts
```

---

## Contributing

### Adding a New Userland Program

1. Create `commands/your_program.asm`
2. Use template from "Userland Program Development" section
3. `%include "userland.inc"`
4. Test with `./build.sh your_program`
5. Add to `build/asm` COMMANDS array if needed

### Coding Standards

- Use `userland.inc` macros instead of inline syscall setup
- Position-independent code (use `USERLAND_START` macro)
- Standardized error handling with `PRINT_USAGE`/`PRINT_ERROR`
- Comment non-obvious code
- Test in QEMU before committing

---

## Further Reading

- `docs/duplicate-code-analysis.md` — Original duplicate code analysis
- `docs/refactoring-summary.md` — Detailed refactoring results
- `lib/userland.inc` — Library source with usage examples
- https://hermes-agent.nousresearch.com/docs — Hermes Agent documentation
