# Handoff: Morse Code Userland Program

**Date:** 2026-06-05  
**Status:** In Progress - Ready for Testing

## Goal
Create an interactive userland program called `morse` that tests Morse code generation. The program should:
1. Accept text input from user (A-Z, 0-9)
2. Display the Morse code equivalent as text (e.g., "SOS" → "... --- ...")
3. Eventually integrate with `sound.inc` to play Morse code as audio

## Current State

### File Location
`/home/janko/dev/code/kernel/src/commands/morse.asm`

### What's Been Done
1. **Multiple iterations** to find working approach:
   - First attempt: Bit-packed Morse table (failed - encoding issues)
   - Second attempt: String-based with pointer table (failed - addressing issues with `[ebp + pointer_table]`)
   - Third attempt: Direct character comparison (current - should work)

2. **Current implementation** uses:
   - Direct `cmp al, 'A'` style comparisons (36 comparisons for A-Z and 0-9)
   - Simple string printing via `print_string` function
   - No complex pointer tables or addressing modes

3. **Verified working**:
   - Enter key detection works (tested with 'X' print - user confirmed 'X' appears)
   - Key echo works
   - Buffer storage works
   - UI drawing works

### Build Command
```bash
cd /home/janko/dev/code && ./asm -r
```

### How to Test
1. Run `./asm -r` from `/home/janko/dev/code`
2. In QEMU, type `morse` at the shell
3. Type some text (e.g., "SOS" or "ABC")
4. Press **Enter**
5. Should display Morse code after the input:
   ```
   Input: SOS
   ... --- ...
   ```

## Expected Behavior
- Type text → characters echo to screen
- Press Enter → newline, then Morse code displayed
- Type 'q' or 'Q' → quit program
- Backspace → delete last character
- Only A-Z, 0-9 are converted (other chars skipped)

## Next Steps
1. **Test current version** - User needs to manually test (QEMU timed out in agent terminal)
2. **If working**: Add sound generation using `sound.inc`
3. **If not working**: Debug why Morse strings aren't printing (likely still an addressing issue)

## Key Technical Details
- Uses `userland.inc` macros (USERLAND_START, SYS_PRINT_CR)
- SYS_PUTCHAR = 0, SYS_GETKEY = 7, SYS_CLS = 4
- All strings addressed with `[ebp + string_name]` for position-independent code
- Buffer stored in `.bss` section (input_buffer resb 64)

## Files Modified
- `/home/janko/dev/code/kernel/src/commands/morse.asm` (main program)
- Build system automatically picks it up from `kernel/src/commands/` directory

## Memory/Context
- User prefers modular includes (sound.inc, morse.inc, etc.)
- User expects clean code in commits (no debug prints)
- x86 OS uses `ebp`-relative addressing for all data access
- SYS_PRINT_CR macro prints string at `[ebp + esi]` and adds newline

## Potential Issues to Check
1. Morse string addresses might still be wrong (try printing just ".-" hardcoded first)
2. String null-termination might be broken
3. The `print_string` function might have issues with `lodsb`

## Quick Debug Version
If Morse still doesn't print, try this minimal test in `.process_input`:
```asm
; Just print a hardcoded string to test
lea esi, [ebp + test_str]
call print_string
```

With `test_str: db ".-", 0` in data section.

---

**Ready for next session to test and complete.**
