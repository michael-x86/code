; =============================================================================
; panic.inc — Complete Panic Handler (Memory Dump for QEMU Monitor)
; =============================================================================
; APPROACH: Write ALL registers + stack dump to physical memory at 0x0000.
; Then YOU can read it from QEMU monitor using:
;   xp /32x 0x0000    (first 128 bytes - registers)
;   xp /32x 0x0040    (stack dump - 128 bytes)
;
; WHY THIS WORKS: Low memory (0x0000-0x003FFFFF) is identity-mapped,
; so virtual 0x0000 = physical 0x0000. No paging issues.
;
; MEMORY LAYOUT at 0x0000:
;   0x0000: Magic "PNIC" (0x504E4943)
;   0x0004: Exception vector (DWORD)
;   0x0008: Error code (DWORD)
;   0x000C: CR2 (DWORD)
;   0x0010: EIP (DWORD)
;   0x0014: CS (DWORD)
;   0x0018: EFLAGS (DWORD)
;   0x001C: EDI (DWORD)
;   0x0020: ESI (DWORD)
;   0x0024: EBP (DWORD)
;   0x0028: ESP (DWORD)
;   0x002C: EBX (DWORD)
;   0x0030: EDX (DWORD)
;   0x0034: ECX (DWORD)
;   0x0038: EAX (DWORD)
;   0x003C: Reserved
;   0x0040: Stack dump (128 bytes = 32 DWORDS)
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; common_exception_handler — entry point for ALL exceptions
; Stack on entry (after pushad in this handler):
;   [esp+00] = EDI
;   [esp+04] = ESI
;   [esp+08] = EBP
;   [esp+12] = ESP (original, before pushad)
;   [esp+16] = EBX
;   [esp+20] = EDX
;   [esp+24] = ECX
;   [esp+28] = EAX
;   [esp+32] = exception vector (pushed by stub)
;   [esp+36] = error code (pushed by stub or CPU)
;   [esp+40] = EIP (pushed by CPU during exception)
;   [esp+44] = CS
;   [esp+48] = EFLAGS
;   [esp+52] = ESP (if CPL change)
;   [esp+56] = SS (if CPL change)
; -----------------------------------------------------------------------------
common_exception_handler:
    cli                     ; 1. Disable interrupts
    pushad                  ; 2. Save all GPRs
    mov eax, esp            ; 3. EAX = pointer to register frame
    call panic_handler       ; 4. Call the actual handler
    
    ; Should never return
.hang:
    hlt
    jmp .hang

; -----------------------------------------------------------------------------
; panic_handler — write ALL info to memory at 0x0000
; Input: EAX = pointer to register frame (after pushad)
; -----------------------------------------------------------------------------
panic_handler:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    
    mov edi, 0x0000          ; EDI = physical address 0x0000 (identity-mapped)
    
    ; === MAGIC NUMBER (to verify the dump is valid) ===
    mov dword [edi], 0x504E4943  ; "PNIC"
    add edi, 4
    
    ; === EXCEPTION VECTOR ===
    mov ebx, [eax+32]       ; Exception vector (from stack frame)
    mov [edi], ebx
    add edi, 4
    
    ; === ERROR CODE ===
    mov ebx, [eax+36]       ; Error code
    mov [edi], ebx
    add edi, 4
    
    ; === CR2 (PAGE FAULT ADDRESS) ===
    mov ebx, cr2
    mov [edi], ebx
    add edi, 4
    
    ; === EIP (FROM CPU EXCEPTION FRAME) ===
    mov ebx, [eax+40]       ; EIP (pushed by CPU)
    mov [edi], ebx
    add edi, 4
    
    ; === CS ===
    mov ebx, [eax+44]
    mov [edi], ebx
    add edi, 4
    
    ; === EFLAGS ===
    mov ebx, [eax+48]
    mov [edi], ebx
    add edi, 4
    
    ; === GPRS (from register frame) ===
    mov ebx, [eax+00]       ; EDI (original)
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+04]       ; ESI
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+08]       ; EBP
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+12]       ; ESP (original, before pushad)
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+16]       ; EBX
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+20]       ; EDX
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+24]       ; ECX
    mov [edi], ebx
    add edi, 4
    
    mov ebx, [eax+28]       ; EAX
    mov [edi], ebx
    add edi, 4
    
    ; === RESERVED (padding) ===
    mov dword [edi], 0x00000000
    add edi, 4
    
    ; === STACK DUMP (128 bytes = 32 DWORDS) ===
    ; Read from original ESP (before pushad)
    mov esi, [eax+12]       ; ESI = original ESP
    mov ecx, 32              ; 32 DWORDS = 128 bytes
.stack_loop:
    mov ebx, [esi]
    mov [edi], ebx
    add esi, 4
    add edi, 4
    dec ecx
    jnz .stack_loop
    
    ; === DONE ===
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; =============================================================================
; END OF panic.inc
; =============================================================================
