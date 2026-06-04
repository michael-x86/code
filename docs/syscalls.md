# Syscall Reference

## Overview

The x86 OS uses `int 0x80` for system calls. All syscalls follow this convention:

**Input:**
- `eax` = syscall number (0-35)
- `ebx`, `ecx`, `edx`, `esi`, `edi` = arguments (syscall-specific)

**Output:**
- `eax` = return value (0 or positive = success, -1 = error)
- All other GPRs preserved by the kernel

**Total syscalls:** 36 (0-35)

---

## I/O Primitives (0-6)

### 0: `sys_putchar` — Print single character
**Input:**
- `ebx` = ASCII character to print

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
mov ebx, 'A'
mov eax, 0
int 0x80        ; prints 'A'
```

---

### 1: `sys_print` — Print NUL-terminated string
**Input:**
- `esi` = pointer to NUL-terminated string

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
lea esi, [ebp + my_string]
mov eax, 1
int 0x80

my_string: db "Hello, World!", 0
```

---

### 2: `sys_print_cr` — Print string with CR termination
**Input:**
- `esi` = pointer to string (0x0D acts as newline)

**Output:**
- `eax` = 0 (success)

**Note:** Treats byte 0x0D (CR) as newline. Used for help text with embedded CRs.

**Example:**
```asm
lea esi, [ebp + help_text]
mov eax, 2
int 0x80

help_text: db "Command1", 13, "Command2", 13, 0
```

---

### 3: `sys_newline` — Print newline
**Input:** None

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
mov eax, 3
int 0x80        ; prints newline
```

---

### 4: `sys_cls` — Clear screen
**Input:** None

**Output:**
- `eax` = 0 (success)

**Note:** Clears screen below the pinned banner, resets cursor to start of row 1.

**Example:**
```asm
mov eax, 4
int 0x80        ; clear screen
```

---

### 5: `sys_print_hex` — Print value as 8-digit hex
**Input:**
- `ebx` = 32-bit value to print

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
mov ebx, 0xDEADBEEF
mov eax, 5
int 0x80        ; prints "DEADBEEF"
```

---

### 6: `sys_print_int` — Print value as decimal
**Input:**
- `ebx` = unsigned 32-bit value to print

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
mov ebx, 12345
mov eax, 6
int 0x80        ; prints "12345"
```

---

## Input & Time (7-9)

### 7: `sys_get_key` — Read keyboard buffer
**Input:** None

**Output:**
- `eax` = ASCII byte (0 if no key available)

**Example:**
```asm
mov eax, 7
int 0x80
test eax, eax
jz .no_key
; eax has the key
```

---

### 8: `sys_get_tick` — Get system tick count
**Input:** None

**Output:**
- `eax` = tick count (100 Hz, increments every 10ms)

**Example:**
```asm
mov eax, 8
int 0x80
; eax = current tick count
```

---

### 9: `sys_shutdown` — Shutdown QEMU
**Input:** None

**Output:** Does not return

**Note:** Uses QEMU isa-debug-exit device at port 0xF4.

**Example:**
```asm
mov eax, 9
int 0x80        ; shuts down QEMU
```

---

## Raw Memory Access (10-11)

### 10: `sys_read_mem` — Read dword from memory
**Input:**
- `ebx` = virtual address

**Output:**
- `eax` = dword at `[ebx]`

**Example:**
```asm
mov ebx, 0x12345678
mov eax, 10
int 0x80
; eax = dword at address 0x12345678
```

---

### 11: `sys_getcwd` — Get current working directory
**Input:**
- `edi` = destination buffer (must be large enough)

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
lea edi, [ebp + cwd_buf]
mov eax, 11
int 0x80
```

---

## Filesystem (12-21)

### 12: `sys_chdir` — Change directory
**Input:**
- `esi` = path string

**Output:**
- `eax` = 0 (success), -1 (failure: not a directory)

**Example:**
```asm
lea esi, [ebp + dir_path]
mov eax, 12
int 0x80
cmp eax, -1
je .not_a_dir

dir_path: db "/home", 0
```

---

### 13: `sys_list_dir` — List directory entries
**Input:**
- `ebx` = entry index (0-based)
- `edi` = destination buffer for filename (at least 13 bytes)

**Output:**
- `eax` = file type (1=file, 2=directory), -1 (EOF)

**Example:**
```asm
xor ebx, ebx        ; index 0
lea edi, [ebp + name_buf]
mov eax, 13
int 0x80
cmp eax, -1
je .end_of_dir
; eax = file type, name_buf = filename
```

---

### 14: `sys_get_arg` — Get command-line argument
**Input:**
- `ebx` = argument index (0=program name, 1=first arg)
- `edi` = destination buffer

**Output:**
- `eax` = 0 (success), -1 (index out of range)

**Example:**
```asm
mov ebx, 1          ; first argument
lea edi, [ebp + arg_buf]
mov eax, 14
int 0x80
cmp eax, -1
je .no_arg
; arg_buf now contains the argument
```

---

### 15: `sys_stat` — Get file status
**Input:**
- `esi` = path string
- `edi` = 12-byte info buffer

**Output:**
- `eax` = 0 (success), -1 (not found)
- `info[0]` = file type (0=file, 1=directory)
- `info[4]` = file size in bytes

**Example:**
```asm
lea esi, [ebp + file_path]
lea edi, [ebp + info_buf]
mov eax, 15
int 0x80
cmp eax, -1
je .not_found
; info_buf now has file info

file_path: db "/home/janko/file.txt", 0
```

---

### 16: `sys_print_n` — Print N bytes
**Input:**
- `esi` = buffer pointer
- `ecx` = byte count

**Output:**
- `eax` = 0 (success)

**Note:** Translates LF->newline, CR->skip, TAB->space.

**Example:**
```asm
lea esi, [ebp + buffer]
mov ecx, 100
mov eax, 16
int 0x80
```

---

### 17: `sys_create` — Create file
**Input:**
- `esi` = path string

**Output:**
- `eax` = 0 (success), -1 (failure)

**Example:**
```asm
lea esi, [ebp + new_file]
mov eax, 17
int 0x80
cmp eax, -1
je .create_failed

new_file: db "/home/janko/new.txt", 0
```

---

### 18: `sys_write` — Write to file
**Input:**
- `esi` = path string
- `ebx` = data buffer
- `ecx` = byte count

**Output:**
- `eax` = 0 (success), -1 (failure)

**Example:**
```asm
lea esi, [ebp + file_path]
lea ebx, [ebp + data]
mov ecx, 13
mov eax, 18
int 0x80

data: db "Hello, World!", 13, 10, 0
```

---

### 19: `sys_unlink` — Delete file
**Input:**
- `esi` = path string

**Output:**
- `eax` = 0 (success), -1 (failure: not a file)

**Example:**
```asm
lea esi, [ebp + file_to_delete]
mov eax, 19
int 0x80
```

---

### 20: `sys_mkdir` — Create directory
**Input:**
- `esi` = path string

**Output:**
- `eax` = 0 (success), -1 (failure)

**Example:**
```asm
lea esi, [ebp + new_dir]
mov eax, 20
int 0x80

new_dir: db "/home/janko/newdir", 0
```

---

### 21: `sys_rmdir` — Remove empty directory
**Input:**
- `esi` = path string

**Output:**
- `eax` = 0 (success), -1 (failure: not empty or not a directory)

**Example:**
```asm
lea esi, [ebp + dir_to_remove]
mov eax, 21
int 0x80
```

---

## Process Info (22)

### 22: `sys_get_ps_info` — Get process info
**Input:**
- `ebx` = 16-byte destination buffer

**Output:**
- `eax` = 0 (success), -1 (ebx is NULL)
- `buf[0]` = current task ID
- `buf[4]` = task0 ESP
- `buf[8]` = task1 ESP
- `buf[12]` = task2 ESP

**Example:**
```asm
lea ebx, [ebp + ps_buf]
mov eax, 22
int 0x80
cmp eax, -1
je .error
; ps_buf now has process info
```

---

## Page-Level Heap (23-24)

### 23: `sys_alloc_pages` — Allocate pages
**Input:**
- `ecx` = byte count (will be rounded up to page boundary)

**Output:**
- `eax` = virtual address (success), -1 (failure)

**Example:**
```asm
mov ecx, 8192        ; 2 pages = 8192 bytes
mov eax, 23
int 0x80
cmp eax, -1
je .out_of_memory
; eax = allocated virtual address
```

---

### 24: `sys_free_pages` — Free allocated pages
**Input:**
- `ebx` = virtual address from sys_alloc_pages

**Output:**
- `eax` = 0 (success), -1 (not found in allocation table)

**Example:**
```asm
mov ebx, eax         ; address from sys_alloc_pages
mov eax, 24
int 0x80
```

---

## Raw Memory Write (25)

### 25: `sys_write_mem` — Write byte to memory
**Input:**
- `ebx` = virtual address
- `ecx` = byte value (low 8 bits used)

**Output:**
- `eax` = 0 (success)

**Example:**
```asm
mov ebx, 0x12345678
mov ecx, 0xAB
mov eax, 25
int 0x80        ; writes 0xAB to address 0x12345678
```

---

## Parsing (26-27)

### 26: `sys_hex2int` — Parse hex string to integer
**Input:**
- `esi` = hex string (supports optional "0x" prefix)

**Output:**
- `eax` = parsed value

**Example:**
```asm
lea esi, [ebp + hex_str]
mov eax, 26
int 0x80
; eax = parsed value

hex_str: db "0xDEADBEEF", 0
```

---

### 27: `sys_asc2int` — Parse decimal string to integer
**Input:**
- `esi` = decimal string

**Output:**
- `edx` = parsed value (note: returns in edx, not eax!)

**Example:**
```asm
lea esi, [ebp + dec_str]
mov eax, 27
int 0x80
; edx = parsed value (12345)

dec_str: db "12345", 0
```

---

## Introspection (28-31)

### 28: `sys_get_proc_info` — Get current process info
**Input:**
- `ebx` = 36-byte destination buffer

**Output:**
- `eax` = 0 (success), -1 (ebx is NULL)
- `buf[0]` = exec_vbase (program load address)
- `buf[4]` = exec_pages (program size in pages)
- `buf[8..39]` = exec_name (32 bytes, NUL-padded)

**Example:**
```asm
lea ebx, [ebp + proc_info]
mov eax, 28
int 0x80
```

---

### 29: `sys_itoa` — Integer to ASCII (internal use)
**Input:**
- `eax` = value to convert
- `edi` = destination buffer

**Output:**
- `edi` = pointer to byte after written NUL

**Note:** Mainly for kernel internal use, but accessible from userland.

---

### 30: `sys_get_config` — Get system configuration
**Input:**
- `ebx` = key (0=timezone, 1=datefmt, 2=timefmt, 3=layout)
- `edi` = destination buffer (NUL-terminated decimal string)

**Output:**
- `eax` = 0 (success), -1 (bad key)

**Example:**
```asm
mov ebx, 0          ; get timezone
lea edi, [ebp + cfg_buf]
mov eax, 30
int 0x80
```

---

### 31: `sys_stack_dump` — Dump syscall stack frame
**Input:** None

**Output:** Prints register dump and 8 dwords from stack

**Note:** Useful for debugging crashes from userland.

**Example:**
```asm
mov eax, 31
int 0x80        ; prints register dump
```

---

## Malloc-Style Heap (32-33)

### 32: `sys_alloc` — Allocate memory (page-count based)
**Input:**
- `ebx` = page count

**Output:**
- `eax` = virtual address (success), -1 (failure)

**Note:** Similar to `sys_alloc_pages` but takes page count directly.

**Example:**
```asm
mov ebx, 2          ; 2 pages
mov eax, 32
int 0x80
cmp eax, -1
je .out_of_memory
```

---

### 33: `sys_dealloc` — Free memory from sys_alloc
**Input:**
- `ebx` = virtual address from sys_alloc

**Output:**
- `eax` = 0 (success), -1 (not found in allocation table)

**Example:**
```asm
mov ebx, eax         ; address from sys_alloc
mov eax, 33
int 0x80
```

---

## Peek/Poke (34-35)

### 34: `sys_peek` — Read byte from memory
**Input:**
- `ebx` = virtual address

**Output:**
- `eax` = byte at `[ebx]` (zero-extended)

**Example:**
```asm
mov ebx, 0x12345678
mov eax, 34
int 0x80
; eax = byte at address 0x12345678
```

---

### 35: `sys_poke` — Write byte to memory
**Input:**
- `ebx` = virtual address
- `ecx` = byte value (low 8 bits used)

**Output:**
- `eax` = 0 (success)

**Note:** Identical to `sys_write_mem` (syscall 25), but uses different register for value.

**Example:**
```asm
mov ebx, 0x12345678
mov ecx, 0xAB
mov eax, 35
int 0x80
```

---

## Macros from `userland.inc`

For convenience, the `userland.inc` library provides macros that wrap these syscalls:

```asm
; Inline syscall macros (don't preserve registers)
SYS_GET_ARG        ; instead of: mov eax, 14 / int 0x80
SYS_PRINT           ; instead of: mov eax, 1 / int 0x80
SYS_PRINT_CR        ; instead of: mov eax, 2 / int 0x80
SYS_NEWLINE         ; instead of: mov eax, 3 / int 0x80
SYS_PRINT_HEX       ; instead of: mov eax, 5 / int 0x80
SYS_ASC2INT         ; instead of: mov eax, 27 / int 0x80
SYS_STAT            ; instead of: mov eax, 15 / int 0x80
SYS_PRINT_N         ; instead of: mov eax, 16 / int 0x80

; Helper macros
GET_ARG index, buf  ; e.g., GET_ARG 1, arg_buf
PARSE_INT           ; after loading esi with string
```

See `docs/DEVELOPER.md` for more details on using `userland.inc`.

---

## Error Handling

Most syscalls return:
- **0 or positive value:** Success
- **-1:** Error (specific meaning varies by syscall)

Always check for -1 after syscalls that can fail:

```asm
mov eax, 14        ; sys_get_arg
int 0x80
cmp eax, -1
je .error_handler
```

---

## Complete Syscall Table

| Number | Name | Input | Output |
|--------|------|--------|--------|
| 0 | `sys_putchar` | ebx=char | eax=0 |
| 1 | `sys_print` | esi=string | eax=0 |
| 2 | `sys_print_cr` | esi=string | eax=0 |
| 3 | `sys_newline` | - | eax=0 |
| 4 | `sys_cls` | - | eax=0 |
| 5 | `sys_print_hex` | ebx=value | eax=0 |
| 6 | `sys_print_int` | ebx=value | eax=0 |
| 7 | `sys_get_key` | - | eax=key |
| 8 | `sys_get_tick` | - | eax=ticks |
| 9 | `sys_shutdown` | - | doesn't return |
| 10 | `sys_read_mem` | ebx=addr | eax=value |
| 11 | `sys_getcwd` | edi=buf | eax=0 |
| 12 | `sys_chdir` | esi=path | eax=0/-1 |
| 13 | `sys_list_dir` | ebx=idx, edi=buf | eax=type/-1 |
| 14 | `sys_get_arg` | ebx=idx, edi=buf | eax=0/-1 |
| 15 | `sys_stat` | esi=path, edi=buf | eax=0/-1 |
| 16 | `sys_print_n` | esi=buf, ecx=n | eax=0 |
| 17 | `sys_create` | esi=path | eax=0/-1 |
| 18 | `sys_write` | esi=path, ebx=buf, ecx=n | eax=0/-1 |
| 19 | `sys_unlink` | esi=path | eax=0/-1 |
| 20 | `sys_mkdir` | esi=path | eax=0/-1 |
| 21 | `sys_rmdir` | esi=path | eax=0/-1 |
| 22 | `sys_get_ps_info` | ebx=buf | eax=0/-1 |
| 23 | `sys_alloc_pages` | ecx=bytes | eax=addr/-1 |
| 24 | `sys_free_pages` | ebx=addr | eax=0/-1 |
| 25 | `sys_write_mem` | ebx=addr, ecx=val | eax=0 |
| 26 | `sys_hex2int` | esi=string | eax=value |
| 27 | `sys_asc2int` | esi=string | edx=value |
| 28 | `sys_get_proc_info` | ebx=buf | eax=0/-1 |
| 29 | `sys_itoa` | eax=val, edi=buf | edi=ptr |
| 30 | `sys_get_config` | ebx=key, edi=buf | eax=0/-1 |
| 31 | `sys_stack_dump` | - | prints dump |
| 32 | `sys_alloc` | ebx=pages | eax=addr/-1 |
| 33 | `sys_dealloc` | ebx=addr | eax=0/-1 |
| 34 | `sys_peek` | ebx=addr | eax=byte |
| 35 | `sys_poke` | ebx=addr, ecx=val | eax=0 |

---

## See Also

- `docs/DEVELOPER.md` — Developer guide with examples
- `kernel/src/includes/syscall.inc` — Source code for all syscall handlers
- `lib/userland.inc` — Userland macro library
