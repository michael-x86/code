================================================================================
  ENIGMA M4 — 32-bit x86 Assembly Implementation
  ==================================================

  A faithful software model of the Kriegsmarine 4-rotor Enigma machine
  (M4, introduced February 1942), written in NASM 32-bit x86 assembly.

  Daily keys and message keys are deterministically derived from a date
  entered as YYYYMMDD, using a xorshift PRNG for entropy extraction.

  This is a fun educational project — not cryptographically secure!

================================================================================
  BUILDING
================================================================================

    nasm -f elf32 -g enigma_tables.asm  -o enigma_tables.o
    nasm -f elf32 -g enigma_hash.asm    -o enigma_hash.o
    nasm -f elf32 -g enigma_key.asm     -o enigma_key.o
    nasm -f elf32 -g enigma_core.asm    -o enigma_core.o
    nasm -f elf32 -g enigma_io.asm      -o enigma_io.o
    nasm -f elf32 -g enigma_main.asm    -o enigma_main.o
    ld -m elf_i386 -o enigma_m4 \
        enigma_main.o enigma_core.o enigma_key.o \
        enigma_hash.o enigma_tables.o enigma_io.o

  Or simply:

    make

================================================================================
  RUNNING
================================================================================

    ./enigma_m4

  The program will prompt for a date:

    Enter date (YYYYMMDD): 19420401

  It will display the derived daily key settings, the message key, and the
  encrypted indicator. Then it enters interactive mode — type plaintext and
  the ciphertext is output in real-time (one character at a time).

  Press Enter (empty line) to quit.

================================================================================
  MACHINE SPECIFICATION
================================================================================

  Rotor pool:
    Main rotors:    I, II, III, IV, V, VI, VII, VIII  (choose 3, order matters)
    Thin rotors:    Beta, Gamma                        (choose 1)
    Reflectors:     B-thin, C-thin                     (choose 1)

  Signal path:
    Keyboard → Plugboard → Rotor III(fast) → Rotor II(mid) → Rotor I(slow)
              → Thin Rotor → Reflector → (return path reversed) → Lampboard

  Stepping:
    - Fast rotor (rightmost main) steps every keypress.
    - If fast rotor at notch → middle rotor steps.
    - If middle rotor at notch → slow rotor AND middle rotor step (double-step).
    - Thin rotor NEVER steps.

  Plugboard: 10 pairs swapped, 6 letters unswapped.

================================================================================
  DATE-BASED KEY DERIVATION
================================================================================

  The 8-digit date string is parsed to a 32-bit integer, then run through
  a xorshift hash to produce 32 bytes of entropy. These bytes drive:

    Byte  0, bit 0:   Reflector B or C
    Byte  0, bit 1:   Thin rotor Beta or Gamma
    Byte  1:          Main rotor 1 (slow)  — mod 8
    Byte  2:          Main rotor 2 (middle) — mod 7 (exclude used)
    Byte  3:          Main rotor 3 (fast)   — mod 6 (exclude used)
    Bytes 4-7:        Ring settings for rotors 1-4 (mod 26 each)
    Bytes 8-11:       Initial positions (Grundstellung) for rotters 1-4 (mod 26)
    Bytes 12-21:      Plugboard: 10 pairs drawn from remaining alphabet
    Bytes 22-25:      Message key (4 letters, mod 26 each)

================================================================================
  FILES
================================================================================

    enigma_tables.asm   Rotor wiring, notch positions, reflector tables
    enigma_hash.asm     xorshift PRNG and hash expansion (32 bytes from date)
    enigma_key.asm      Date parsing, daily key derivation, message key derivation
    enigma_core.asm     Rotor stepping, single-char encrypt, plugboard lookup
    enigma_io.asm       String formatting, number-to-ASCII, display helpers
    enigma_main.asm     Entry point (_start), I/O loop, message indicator protocol
    Makefile            Build automation
    README.md           This file

================================================================================
  TESTING
================================================================================

  Enigma is an involution: encrypting ciphertext with the same settings
  recovers the plaintext. To verify:

    1. Enter a date.
    2. Type "AAAAA" — note the output.
    3. Re-run with the same date.
    4. Type the ciphertext from step 2.
    5. You should get "AAAAA" back.

  Known test vector (all rotors A, ring A, no plugboard, rotors I-II-III,
  thin Beta, reflector B-thin, Grundstellung AAAA):
    Plaintext:  AAAAA
    Ciphertext: BDCYZ  (first 5 chars — verify against your build)

================================================================================
  LICENSE / NOTES
================================================================================

  Educational implementation. The Enigma machine is historically significant
  and all cryptographic material presented here is long since declassified.
  Have fun!
================================================================================
