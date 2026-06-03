; ============================================================================
; enigma_hash.asm — xorshift PRNG and hash expansion
; ============================================================================
; Takes a 32-bit seed (the parsed date) and produces 32 bytes of pseudorandom
; output in hash_buf. Uses a xorshift32 core with a simple expansion loop.
;
; This is NOT cryptographically secure — it's a fun deterministic mapping
; from dates to Enigma keys.
;
; Exported symbols:
;   hash_init(seed)       — initialize state from 32-bit date value
;   hash_expand()         — fill hash_buf with 32 pseudorandom bytes
;   hash_buf              — the 32-byte output buffer
; ============================================================================

section .bss

global hash_buf
hash_buf: resb 32           ; 32 bytes of hash output

section .data

; Internal PRNG state
prng_state: dd 0

section .text

global hash_init
global hash_expand
global xorshift32

; ----------------------------------------------------------------------------
; xorshift32 — advance the PRNG state
; Input:  [prng_state] = current 32-bit state
; Output: EAX = new state value (also stored back to [prng_state])
;
; Algorithm (Marsaglia xorshift):
;   x ^= x << 13
;   x ^= x >> 17
;   x ^= x << 5
; ----------------------------------------------------------------------------
xorshift32:
    mov     eax, [prng_state]
    mov     ebx, eax
    shl     ebx, 13
    xor     eax, ebx        ; x ^= x << 13
    mov     ebx, eax
    shr     ebx, 17
    xor     eax, ebx        ; x ^= x >> 17
    mov     ebx, eax
    shl     ebx, 5
    xor     eax, ebx        ; x ^= x << 5
    mov     [prng_state], eax
    ret

; ----------------------------------------------------------------------------
; hash_init — seed the PRNG from a 32-bit date value
; Input: EAX = date as integer (e.g. 19420401)
;
; We apply a mixing step first to avoid degenerate seeds like 20000000.
; ----------------------------------------------------------------------------
hash_init:
    ; Mix the seed to avoid bad states (0 is a fixed point of xorshift)
    mov     ebx, eax
    shl     ebx, 7
    xor     eax, ebx
    add     eax, 0xDEADBEEF
    xor     eax, ebx
    ; Ensure non-zero
    test    eax, eax
    jnz     .nonzero
    mov     eax, 0x12345678
.nonzero:
    mov     [prng_state], eax
    ret

; ----------------------------------------------------------------------------
; hash_expand — generate 32 bytes into hash_buf
; Uses xorshift32 to produce 8 x 32-bit words, stored little-endian.
; ----------------------------------------------------------------------------
hash_expand:
    push    ebx
    push    ecx
    push    edi

    mov     edi, hash_buf
    mov     ecx, 8              ; 8 words = 32 bytes
.expand_loop:
    call    xorshift32          ; EAX = next 32-bit word
    mov     [edi], eax          ; store all 4 bytes
    add     edi, 4
    dec     ecx
    jnz     .expand_loop

    pop     edi
    pop     ecx
    pop     ebx
    ret
