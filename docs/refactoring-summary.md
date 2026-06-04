# Refactoring Summary

## Work Completed

### 1. Created Shared Library
**File:** `/home/janko/dev/code/lib/userland.inc` (243 lines)

**Contents:**
- `USERLAND_START` macro — replaces 3-line _start boilerplate
- Syscall wrapper macros: `SYS_GET_ARG`, `SYS_PRINT`, `SYS_PRINT_CR`, `SYS_NEWLINE`, `SYS_PRINT_HEX`, `SYS_ASC2INT`, `SYS_STAT`, `SYS_PRINT_N`
- Error handling macros: `PRINT_USAGE`, `PRINT_ERROR`, `PRINT_SUCCESS`
- Utility functions: `sys_get_arg`, `sys_print`, `sys_print_cr`, `sys_newline`, `sys_print_hex`, `sys_asc2int`
- Helper macros: `GET_ARG`, `PARSE_INT`
- Standard BSS template macro: `SECTION_BSS_DEFAULT`

### 2. Refactored All Userland Programs
**26 programs** refactored to use `userland.inc`:

```
alloc.asm      argtest.asm   calc.asm      cat.asm
cp.asm         dealloc.asm   dump.asm      elite.asm
enigma.asm     env.asm       exit.asm      gdemo.asm
help.asm       invaders.asm   mkdir.asm     mv.asm
peek.asm       ping.asm       poke.asm      ps.asm
pwd.asm        rmdir.asm      rm.asm        touch.asm
write.asm      zork.asm
```

### 3. Fixed Build Infrastructure
- **`build/asm`** (line 73): Added `-i "${PROJECT_DIR}/lib/"` flag for include path
- **`build.sh`**: Created alternative build script with proper include path
- **`Makefile`**: Created Makefile with `make` and `make <program>` support

---

## Impact Analysis

### Lines of Code Saved
**Before:** Each program had ~10-20 lines of boilerplate
**After:** Each program uses macros (~1-3 lines per pattern)

**Estimated savings:** ~400+ lines across all programs

### Example: `alloc.asm`

**Before (69 lines):**
```asm
_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    ; --- Fetch page count argument ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    mov eax, 14             ; sys_get_arg
    int 0x80
    cmp eax, -1
    je .usage
```

**After (49 lines):**
```asm
%include "userland.inc"

_start:
    USERLAND_START

    ; --- Fetch page count argument ---
    GET_ARG 1, arg_buf
    cmp eax, -1
    je .usage
```

**Reduction:** 29% fewer lines, much more readable

---

## Code Quality Improvements

### 1. Maintainability
- **Before:** Change boilerplate = edit 26 files
- **After:** Change `userland.inc` = all programs updated

### 2. Readability
- **Before:** Inline comments explaining syscalls
- **After:** Self-documenting macro names (`GET_ARG`, `SYS_PRINT`)

### 3. Consistency
- **Before:** Slight variations in error handling across programs
- **After:** Standardized error/usage message macros

### 4. Correctness
- **Before:** Risk of copying boilerplate incorrectly
- **After:** Single source of truth in library

---

## Verification

### Build Test
```bash
$ cd /home/janko/dev/code
$ ./build/asm
[0/6] Populating filesystem content directories...
[1/6] Building commands...
  + bin/exit (68 bytes)
  + bin/help (858 bytes)
  ... (all 26 programs compiled successfully)
[2/6] Generating filesystem image...
...
Build complete!
```

**Result:** All 26 programs compile without errors!

---

## Next Steps (Optional)

### 1. Further Refactoring
- Use `SYS_GET_ARG`, `SYS_PRINT`, etc. macros more consistently
- Replace inline syscall setups with function calls where appropriate
- Add more utility functions to `userland.inc` as needed

### 2. Kernel Include Audit
- Review `kernel/src/includes/*.inc` for cross-file duplication
- Ensure `lib.inc` is being used optimally

### 3. Documentation
- Add examples to `userland.inc` header
- Create `docs/userland-programming.md` guide

---

## Files Modified/Created

### Created:
- `/home/janko/dev/code/lib/userland.inc` — Shared library
- `/home/janko/dev/code/build.sh` — Alternative build script
- `/home/janko/dev/code/Makefile` — Makefile support
- `/home/janko/dev/code/scripts/refactor-userland.py` — Refactoring script
- `/home/janko/dev/code/docs/duplicate-code-analysis.md` — Analysis report

### Modified:
- `/home/janko/dev/code/build/asm` — Fixed include path (line 73)
- `/home/janko/dev/code/commands/*.asm` — All 26 programs refactored

---

## Summary

✅ **Duplicate code eliminated:** ~400+ lines  
✅ **All programs refactored:** 26/26  
✅ **Build verified:** No compilation errors   
✅ **Maintainability improved:** Single source of truth  

The codebase is now significantly cleaner and easier to maintain!
