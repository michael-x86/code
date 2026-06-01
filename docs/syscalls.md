# System Calls

All syscalls use `int 0x80`. Arguments in `ebx`, `ecx`, `edx`, `esi`, `edi`.
Return value in `eax`. Bad syscall number returns `-1`.

Syscalls execute with interrupts disabled (interrupt gate).

## Reference

| #  | Name       | Args                              | Returns                           |
|----|------------|-----------------------------------|-----------------------------------|
| 0  | putchar    | `ebx` = char                      | 0                                 |
| 1  | print      | `esi` = ptr (null-terminated)     | 0                                 |
| 2  | print_cr   | `esi` = ptr (CR=13 → newline)     | 0                                 |
| 3  | newline    | —                                 | 0                                 |
| 4  | cls        | —                                 | 0                                 |
| 5  | print_hex  | `ebx` = value                     | 0                                 |
| 6  | print_int  | `ebx` = signed decimal            | 0                                 |
| 7  | get_key    | —                                 | ASCII code (0 if buffer empty)    |
| 8  | get_tick   | —                                 | PIT tick count (100 Hz)           |
| 9  | shutdown   | —                                 | does not return                   |
| 10 | read_mem   | `ebx` = address                   | dword at [address]                |
| 11 | getcwd     | `edi` = destination buffer        | 0                                 |
| 12 | chdir      | `esi` = path                      | 0 / -1                            |
| 13 | list_dir   | `ebx` = index, `edi` = dst buffer | type byte / -1 (writes basename)  |
| 14 | get_arg    | `ebx` = index, `edi` = dst buffer | 0 / -1 (writes argv[i])           |
| 15 | stat       | `esi` = path, `edi` = info (12B)  | 0 / -1                            |
| 16 | print_n    | `esi` = ptr, `ecx` = length       | 0 (`\n`→newline, `\t`→space)      |
| 17 | create     | `esi` = path                      | 0 / -1                            |
| 18 | write      | `esi` = path, `ebx` = buf, `ecx` = n | 0 / -1                        |
| 19 | unlink     | `esi` = path                      | 0 / -1                            |
| 20 | sys_mkdir  | `esi` = path                      | 0 / -1                            |
| 21 | sys_rmdir  | `esi` = path                      | 0 / -1                            |

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
