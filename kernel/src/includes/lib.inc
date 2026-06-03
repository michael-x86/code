; =============================================================================
; lib.inc — shared string and numeric helpers
; =============================================================================
;
; Pure functions used by the syscall handlers, the shell, the FS layer, and
; the userland program loader. Each routine:
;   - takes its inputs in the call-clobbered registers (esi / edi / eax)
;   - returns the result in eax (or edx for asc2int)
;   - preserves ebx, ecx, edx unless that register is the return value
;   - is position-independent (no absolute addresses)
;
; Keeping these here, rather than inlined into shell.inc or vfs.inc, means
; we have exactly one copy of the str-cmp / hex-parse / decimal-print logic
; — otherwise every module that wants to parse a number would grow its own
; slightly different copy.
; =============================================================================


; -----------------------------------------------------------------------------
; str_eq — case-sensitive string equality
;   in:  esi = str1 (NUL-terminated), edi = str2 (NUL-terminated)
;   out: eax = 0 if equal, 1 if not
; -----------------------------------------------------------------------------
str_eq:
    push esi
    push edi
.lp:
    mov al, [esi]
    mov ah, [edi]
    cmp al, ah
    jne .ne
    test al, al
    jz .eq
    inc esi
    inc edi
    jmp .lp
.eq:
    pop edi
    pop esi
    xor eax, eax
    ret
.ne:
    pop edi
    pop esi
    mov eax, 1
    ret


; -----------------------------------------------------------------------------
; str_len — length of a NUL-terminated string
;   in:  esi = string
;   out: ecx = length (not including the NUL byte)
;
;   We return in ecx (not eax) so the caller's loop can use `loop` directly.
; -----------------------------------------------------------------------------
str_len:
    push esi
    xor ecx, ecx
.lp:
    cmp byte [esi], 0
    je .done
    inc esi
    inc ecx
    jmp .lp
.done:
    pop esi
    ret


; -----------------------------------------------------------------------------
; str_copy — copy a NUL-terminated string, including the terminator
;   in:  esi = source, edi = dest
;   clobbers: eax
; -----------------------------------------------------------------------------
str_copy:
    push eax
    push esi
    push edi
.lp:
    lodsb
    stosb
    test al, al
    jnz .lp
    pop edi
    pop esi
    pop eax
    ret


; -----------------------------------------------------------------------------
; basename_if_child — check if `path` is a direct child of `cwd`
;   in:  esi = path, edi = cwd
;   out: eax = pointer to basename within `path`, or 0 if not a direct child
;
;   Used by the FS layer to decide whether a relative path can be resolved
;   against cwd by a single dir_lookup call. Multi-component relative paths
;   (e.g. "foo/bar") are NOT children and yield 0.
; -----------------------------------------------------------------------------
basename_if_child:
    push ebx
    push edx
    push esi
    push edi
    cmp byte [edi + 1], 0
    je .cwd_root
.mp:
    mov al, [edi]
    test al, al
    jz .after_cwd
    mov bl, [esi]
    cmp al, bl
    jne .nope
    inc esi
    inc edi
    jmp .mp
.after_cwd:
    cmp byte [esi], '/'
    jne .nope
    inc esi
    cmp byte [esi], 0
    je .nope
    mov ebx, esi
    mov edx, esi
.scan:
    mov al, [edx]
    test al, al
    jz .yes
    cmp al, '/'
    je .nope
    inc edx
    jmp .scan
.yes:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret
.cwd_root:
    cmp byte [esi], '/'
    jne .nope
    cmp byte [esi + 1], 0
    je .nope
    mov ebx, esi
    inc ebx
    mov edx, ebx
.sr:
    mov al, [edx]
    test al, al
    jz .root_ok
    cmp al, '/'
    je .nope
    inc edx
    jmp .sr
.root_ok:
    mov eax, ebx
    pop edi
    pop esi
    pop edx
    pop ebx
    ret
.nope:
    xor eax, eax
    pop edi
    pop esi
    pop edx
    pop ebx
    ret


; -----------------------------------------------------------------------------
; asc2int — parse a decimal ASCII string
;   in:  esi = string (digits, optional leading whitespace)
;   out: edx = parsed integer (returned in EDX, not EAX, to match the
;        convention used by the shell's alloc_cmd caller)
;
;   Stops at the first non-digit (including NUL and space). Stops on
;   overflow implicitly — the value is computed mod 2^32.
; -----------------------------------------------------------------------------
asc2int:
    xor edx, edx
.loop:
    movzx eax, byte [esi]
    test al, al
    jz .done
    cmp al, ' '
    je .done
    sub eax, '0'
    imul edx, edx, 10
    add edx, eax
    inc esi
    jmp .loop
.done:
    ret


; -----------------------------------------------------------------------------
; hex2int — parse a hex ASCII string (with optional 0x / 0X prefix)
;   in:  esi = string
;   out: eax = parsed value
; -----------------------------------------------------------------------------
hex2int:
    xor eax, eax
    xor ebx, ebx
    mov bl, [esi]
    cmp bl, '0'
    jne .parse
    cmp byte [esi + 1], 'x'
    je .skip_prefix
    cmp byte [esi + 1], 'X'
    jne .parse
.skip_prefix:
    add esi, 2
.parse:
    movzx ebx, byte [esi]
    test bl, bl
    jz .done_hex
    cmp bl, '0'
    jb .done_hex
    cmp bl, '9'
    jle .digit
    cmp bl, 'A'
    jb .lower
    cmp bl, 'F'
    jle .upper
    cmp bl, 'a'
    jb .done_hex
    cmp bl, 'f'
    ja .done_hex
.lower:
    sub bl, 'a' - 10
    jmp .shift
.upper:
    sub bl, 'A' - 10
    jmp .shift
.digit:
    sub bl, '0'
.shift:
    shl eax, 4
    or eax, ebx
    inc esi
    jmp .parse
.done_hex:
    ret


; -----------------------------------------------------------------------------
; print_hex_byte — print a single byte as 2 hex digits
;   in:  al = byte
; -----------------------------------------------------------------------------
print_hex_byte:
    push eax
    shr al, 4
    and al, 0x0F
    call print_hex_nibble
    mov al, byte [esp]
    and al, 0x0F
    call print_hex_nibble
    pop eax
    ret


; -----------------------------------------------------------------------------
; print_hex_nibble — print a 4-bit value as one hex digit
;   in:  al = value 0..15
;
;   Tiny helper, kept private to lib.inc — its job is to render '0'..'9',
;   'A'..'F' for the high and low nibbles of a byte.
; -----------------------------------------------------------------------------
print_hex_nibble:
    cmp al, 9
    jbe .num
    add al, 55             ; 'A' - 10 = 55
    jmp .out
.num:
    add al, '0'
.out:
    call putchar
    ret


; -----------------------------------------------------------------------------
; print_hex_dword — print a 32-bit value as 8 hex digits
;   in:  eax = value
;
;   We rotate-and-mask instead of shift-and-mask so we don't have to copy
;   eax to a scratch register — the value lives on the stack between nibbles.
; -----------------------------------------------------------------------------
print_hex_dword:
    push eax
    push ecx
    mov ecx, 8
.hloop:
    rol dword [esp + 4], 4
    mov al, byte [esp + 4]
    and al, 0x0F
    cmp al, 9
    jbe .num
    add al, 55
    jmp .out
.num:
    add al, '0'
.out:
    call putchar
    loop .hloop
    pop ecx
    pop eax
    ret


; -----------------------------------------------------------------------------
; print_int_decimal — print an unsigned 32-bit integer in base 10
;   in:  eax = value
;
;   We do the divide-by-10 / push-digit / pop-digit dance to print in the
;   correct order without a temporary buffer. For 0 we print "0" explicitly.
; -----------------------------------------------------------------------------
print_int_decimal:
    push eax
    push ebx
    push ecx
    push edx
    mov ecx, 0
    mov ebx, 10
    test eax, eax
    jnz .divide
    mov al, '0'
    call putchar
    jmp .fin
.divide:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .divide
.emit:
    pop edx
    mov al, dl
    add al, '0'
    call putchar
    loop .emit
.fin:
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret
