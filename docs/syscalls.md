# System Calls

All syscalls use `int 0x80`. Arguments in `ebx`, `ecx`, `edx`, `esi`, `edi`.
Return value in `eax`. Bad syscall number returns `-1`.

Syscalls execute with interrupts disabled (interrupt gate, DPL=3).

## Reference

| #  | Name             | Args                                       | Returns                              |
|----|------------------|--------------------------------------------|--------------------------------------|
| 0  | putchar          | `ebx` = char                               | 0                                    |
| 1  | print            | `esi` = ptr (null-terminated)              | 0                                    |
| 2  | print_cr         | `esi` = ptr (CR=13 → newline)              | 0                                    |
| 3  | newline          | —                                          | 0                                    |
| 4  | cls              | —                                          | 0                                    |
| 5  | print_hex        | `ebx` = value                              | 0                                    |
| 6  | print_int        | `ebx` = signed decimal                     | 0                                    |
| 7  | get_key          | —                                          | ASCII code (0 if buffer empty)       |
| 8  | get_tick         | —                                          | PIT tick count (100 Hz)              |
| 9  | shutdown         | —                                          | does not return                      |
| 10 | read_mem         | `ebx` = address                            | dword at [address]                   |
| 11 | getcwd           | `edi` = destination buffer                 | 0                                    |
| 12 | chdir            | `esi` = path                               | 0 / -1                               |
| 13 | list_dir         | `ebx` = index, `edi` = dst buffer          | type byte / -1 (writes basename)     |
| 14 | get_arg          | `ebx` = index, `edi` = dst buffer          | 0 / -1 (writes argv[i])              |
| 15 | stat             | `esi` = path, `edi` = info (12 B)          | 0 / -1                               |
| 16 | print_n          | `esi` = ptr, `ecx` = length                | 0 (`\n`→newline, `\t`→space)         |
| 17 | create           | `esi` = path                               | 0 / -1                               |
| 18 | write            | `esi` = path, `ebx` = buf, `ecx` = n       | 0 / -1                               |
| 19 | unlink           | `esi` = path                               | 0 / -1                               |
| 20 | mkdir            | `esi` = path                               | 0 / -1                               |
| 21 | rmdir            | `esi` = path                               | 0 / -1                               |
| 22 | get_ps_info      | `ebx` = 16-byte dst                        | 0 / -1 (writes current_task, 3 ESPs) |
| 23 | alloc_pages      | `ecx` = byte count                         | vaddr / -1                           |
| 24 | free_pages       | `ebx` = vaddr                              | 0 / -1                               |
| 25 | write_mem        | `ebx` = addr, `ecx` = byte value           | 0                                    |
| 26 | hex2int          | `esi` = string (optional `0x` prefix)      | parsed value                         |
| 27 | asc2int          | `esi` = decimal string                     | parsed value                         |
| 28 | get_proc_info    | `ebx` = 36-byte dst                        | 0 / -1 (writes exec vbase, pages, name) |
| 29 | itoa             | `eax` = value, `edi` = dst                 | `edi` advanced past NUL              |
| 30 | get_config       | `ebx` = key (0..3), `edi` = dst            | 0 / -1                               |
| 31 | stack_dump       | —                                          | 0 (prints regs + 8 dwords of stack)  |
| 32 | alloc            | `ebx` = page count                         | vaddr / -1                           |
| 33 | dealloc          | `ebx` = vaddr                              | 0 / -1                               |
| 34 | peek             | `ebx` = addr                               | byte at [addr]                       |
| 35 | poke             | `ebx` = addr, `ecx` = byte                 | 0                                    |

## Notes

- **#22 `get_ps_info`**: writes a 16-byte record at `[ebx]`: `[0..3]` =
  `current_task`, `[4..7]` = `task0_esp`, `[8..11]` = `task1_esp`,
  `[12..15]` = `task2_esp`.
- **#23 `alloc_pages`** vs **#32 `alloc`**: same function, different unit
  convention. `#23` takes bytes, `#32` takes pages. Both produce a
  kernel-managed virtual region.
- **#26 `hex2int`** handles an optional `0x` / `0X` prefix and accepts both
  upper- and lower-case hex digits. Stops at the first non-hex byte.
- **#30 `get_config`** keys: `0` = timezone, `1` = date format,
  `2` = time format, `3` = keyboard layout. Writes the value as a
  null-terminated decimal string into the buffer at `edi`.
- **#34 `peek`** and **#35 `poke`** operate on a single byte. Use them for
  poking VGA memory, hardware registers, or user-space buffers.
- **#31 `stack_dump`** is a debug aid — it prints the current register set
  and 8 dwords of the kernel stack via VGA. Useful from inside the kernel
  or from a debug shell command.

## Usage Example (NASM)

```nasm
; print a string
mov esi, my_string
mov eax, 1          ; sys_print
int 0x80

; get a keypress
mov eax, 7          ; sys_get_key
int 0x80
; eax = ASCII code (0 if none)

; create a file
mov esi, filename
mov eax, 17         ; sys_create
int 0x80

; write to a file
mov esi, filename
mov ebx, buffer
mov ecx, length
mov eax, 18         ; sys_write
int 0x80
```

## Adding a new syscall

1. Pick the next free number in `kernel/constants.inc` (under
   `--- Syscall numbers ---`) and bump `SYSCALL_COUNT`.
2. Add the handler in `kernel/syscall.inc` with the standard prologue
   (push/pop the five caller-saved registers around any nested calls).
3. Add the entry to `syscall_table` in `kernel/data.inc` with a comment
   showing the number, args, and return shape.
4. Update the table on this page.
5. Add a test program in `commands/` if the syscall is user-visible.
