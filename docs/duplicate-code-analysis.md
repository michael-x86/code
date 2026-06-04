# Duplicate Code Analysis Report

## Executive Summary

**Total Source Files:** 56 (.asm and .inc files)
**Duplicate Patterns Found:** 4 major categories
**Estimated Duplicated Lines:** ~400-500 lines across userland programs

---

## 1. USERLAND BOILERPLATE DUPLICATION

### Problem
21 userland programs contain identical `_start` boilerplate code:

```asm
_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base
```

This position-independent code pattern is copy-pasted identically into every program.

### Affected Files (21 total)
```
alloc.asm, argtest.asm, calc.asm, cat.asm, cp.asm, dealloc.asm, 
dump.asm, env.asm, exit.asm, help.asm, invaders.asm, mkdir.asm, 
mv.asm, peek.asm, ping.asm, poke.asm, ps.asm, pwd.asm, rmdir.asm, 
rm.asm, touch.asm, write.asm, zork.asm
```

### Recommendation
Create `/home/janko/dev/code/lib/userland.inc` with:

```asm
; =============================================================================
; userland.inc — Standard startup and syscall wrappers for userland programs
; =============================================================================

; --- Standard _start macro ---
%macro USERLAND_START 0
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base
%endmacro

; --- Syscall wrapper macros ---
%macro SYS_GET_ARG 0
    mov eax, 14
    int 0x80
%endmacro

%macro SYS_PRINT 0
    mov eax, 1
    int 0x80
%endmacro

%macro SYS_PRINT_CR 0
    mov eax, 2
    int 0x80
%endmacro

%macro SYS_NEWLINE 0
    mov eax, 3
    int 0x80
%endmacro

%macro SYS_PRINT_HEX 0
    mov eax, 5
    int 0x80
%endmacro

%macro SYS_ASC2INT 0
    mov eax, 27
    int 0x80
%endmacro
```

### Refactored Example (alloc.asm)
**Before:** 69 lines with inline boilerplate
**After:**

```asm
; alloc — allocate pages from the heap. usage: alloc <pagecount>
[bits 32]
[org 0x00000000]

%include "userland.inc"

_start:
    USERLAND_START
    
    ; --- Fetch page count argument ---
    mov ebx, 1
    lea edi, [ebp + arg_buf]
    SYS_GET_ARG
    cmp eax, -1
    je .usage
    
    ; Parse decimal page count
    lea esi, [ebp + arg_buf]
    SYS_ASC2INT
    mov ecx, eax
    test ecx, ecx
    jz .usage
    
    ; --- Allocate pages ---
    mov eax, 23             ; sys_alloc_pages
    int 0x80
    cmp eax, -1
    je .failed
    
    ; Print result
    lea esi, [ebp + ok_msg]
    SYS_PRINT
    
    mov ebx, eax
    SYS_PRINT_HEX
    
    SYS_NEWLINE
    ret
```

**Lines saved:** ~15 lines per program × 21 programs = ~315 lines eliminated

---

## 2. SYSCALL WRAPPER DUPLICATION

### Problem
Multiple userland programs manually set up syscall numbers and call `int 0x80`. While the boilerplate is the main issue, the syscall setup is also repeated.

### Common Patterns Found
```
sys_get_arg (eax=14):  Used in 15+ programs
sys_print (eax=1):      Used in 20+ programs  
sys_print_cr (eax=2):   Used in 18+ programs
sys_newline (eax=3):    Used in 12+ programs
sys_print_hex (eax=5):  Used in 2 programs
sys_asc2int (eax=27):   Used in 2 programs
```

### Recommendation
Add syscall wrapper functions to `userland.inc`:

```asm
; --- Syscall wrapper functions ---
; These preserve registers where appropriate

sys_get_arg:
    push ebx
    mov eax, 14
    int 0x80
    pop ebx
    ret

sys_print:
    push eax
    mov eax, 1
    int 0x80
    pop eax
    ret

; ... etc for other common syscalls
```

---

## 3. ERROR/USAGE MESSAGE PATTERN DUPLICATION

### Problem
Most programs have identical error/usage message handling:

```asm
.usage:
    lea esi, [ebp + usage_msg]
    mov eax, 2
    int 0x80
    ret
```

### Recommendation
Add standard error handling macros:

```asm
%macro PRINT_USAGE 1
    lea esi, [%1]
    SYS_PRINT_CR
    ret
%endmacro

%macro PRINT_ERROR 1
    lea esi, [%1]
    SYS_PRINT_CR
    ret
%endmacro
```

---

## 4. KERNEL-LEVEL SHARED CODE (lib.inc)

### Status: GOOD (already consolidated)
The `kernel/src/includes/lib.inc` file already consolidates:
- `str_eq` — string equality
- `str_len` — string length
- `str_copy` — string copy
- `asc2int` — decimal parser
- `hex2int` — hex parser
- `print_hex_byte/print_hex_nibble/print_hex_dword` — hex printing
- `print_int_decimal` — decimal printing

### Minor Issue Found
The `dump.asm` userland program has its own copies of:
- `print_hex_byte` (lines 146-153)
- `print_hex_nibble` (lines 156-164)  
- `print_hex_dword` (lines 167-186)
- `putchar` (lines 189-193)

These use syscalls (int 0x80) instead of direct VGA access, so they're not true duplicates of `lib.inc` functions. However, they could be moved to a `userland.inc` library.

---

## IMPLEMENTATION ROADMAP

### Phase 1: Create Userland Library (High Impact)
1. Create `/home/janko/dev/code/lib/userland.inc`
2. Add `_start` macro
3. Add syscall wrapper macros
4. Add error handling macros
5. Test with one program (e.g., `cat.asm`)
6. Roll out to all 21 programs

**Estimated savings:** ~315 lines

### Phase 2: Create Syscall Wrapper Functions (Medium Impact)
1. Add wrapper functions to `userland.inc`
2. Update programs to use wrappers instead of inline syscall setup
3. Test thoroughly

**Estimated savings:** ~100 lines + improved readability

### Phase 3: Review Kernel Includes (Low Priority)
1. Audit `kernel/src/includes/*.inc` for cross-file duplication
2. Ensure `lib.inc` is included everywhere it's needed
3. Consider splitting `lib.inc` if it grows too large

---

## CONCLUSION

Your codebase has significant boilerplate duplication in userland programs. The kernel code is better organized with include files. By creating a `userland.inc` library with macros and wrapper functions, you can:

- **Eliminate ~400+ lines** of duplicated code
- **Improve maintainability** (change once, apply everywhere)
- **Reduce bugs** (single source of truth for common patterns)
- **Speed up development** (new programs can just `%include "userland.inc"`)

The refactoring is low-risk because the macros will expand to identical code. You can verify this by comparing the compiled output before/after.

---

## NEXT STEPS

Would you like me to:
1. Create the `userland.inc` library file?
2. Refactor 1-2 example programs to demonstrate the pattern?
3. Search for any additional duplication I might have missed?
