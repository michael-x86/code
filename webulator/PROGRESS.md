# Webulator Progress Log

## Session: 2026-06-06

### Goal
Implement and test x86 CPU instructions for the webulator project - an x86 emulator that runs in the browser to execute the user's OS kernel.

### Completed (Previous Sessions)

1. **Fixed INT 3 (0xCC) handling**
   - Bug: `this.regs.eip++` was incorrectly incrementing EIP after `handleInt(3)` already set EIP to the handler address
   - Fix: Removed the erroneous EIP increment in the 0xCC opcode handler
   - Status: TEST PASSED

2. **Fixed INT imm8 (0xCD) handling**
   - Cleaned up comments to clarify EIP management around interrupt handling
   - Status: TEST PASSED

3. **Fixed ROL (Rotate Left) flags**
   - CF flag: Changed to use LSB of final result `(result & 1)` instead of MSB `(result >> (bitWidth - 1)) & 1`
   - OF flag: Fixed to use XOR of sign bit and CF for 1-bit rotations
   - Status: FIXED

4. **Fixed ROL test (ModR/M byte error)**
   - Bug: Test was writing ModR/M byte 0xE0 (reg=4 = SHL) instead of 0xC0 (reg=0 = ROL)
   - The ROL implementation was actually correct - the test encoding was wrong
   - Fix: Changed ModR/M byte from 0xE0 to 0xC0 in test-cpu5.js
   - Status: TEST PASSED (all 16 tests now pass)

### Completed (This Session)

5. **Implemented MOV r8, imm8 (0xB0-0xB7)**
   - Short form for loading immediate values into 8-bit registers (AL, CL, DL, BL, AH, CH, DH, BH)
   - Status: IMPLEMENTED

6. **Implemented arithmetic with immediate (0x80, 0x81, 0x83)**
   - Handles ADD, OR, ADC, SBB, AND, SUB, XOR, CMP with immediate values
   - Supports 8-bit and 32-bit operands with sign extension
   - Status: IMPLEMENTED

7. **Implemented PUSHAD (0x60) and POPAD (0x61)**
   - Pushes/pops all general-purpose 32-bit registers in specific order
   - Status: IMPLEMENTED

8. **Implemented LOOP instructions (0xE0, 0xE1, 0xE2)**
   - LOOPNE/LOOPNZ, LOOPE/LOOPZ, and LOOP
   - Decrements ECX and jumps if ECX ≠ 0 (and ZF condition met for conditional versions)
   - Status: IMPLEMENTED

9. **Implemented ADD EAX, imm32 (0x05)**
   - Short form for adding immediate to EAX
   - Status: IMPLEMENTED

10. **Implemented AND instructions (0x20-0x25, 0x80/1/3 reg=4)**
    - AND r/m, r; AND r, r/m (8-bit and 32-bit)
    - AND AL, imm8; AND EAX, imm32
    - AND r/m, imm (via arithmetic immediate handler)
    - Status: IMPLEMENTED

11. **Implemented OR EAX/AL, imm32/imm8 (0x0C, 0x0D)**
    - Short forms for OR with immediate values
    - Status: IMPLEMENTED

12. **Implemented short-form opcodes for EAX**
    - OR EAX, imm32 (0x0d)
    - AND EAX, imm32 (0x25)
    - SUB EAX, imm32 (0x2d)
    - XOR EAX, imm32 (0x35)
    - CMP EAX, imm32 (0x3d)
    - Status: IMPLEMENTED

13. **Implemented ADC (Add with Carry) instructions**
    - ADC r/m32, r32 (0x11) and ADC r32, r/m32 (0x13)
    - ADC r/m8, r8 (0x10) and ADC r8, r/m8 (0x12)
    - Handles CF flag correctly in addition
    - Status: IMPLEMENTED

### Test Results

**CPU Test Suite (test-cpu5.js):**
```
Passed: 16
Failed: 0
Total: 16
```

**Kernel Execution Test (test-kernel.js):**
- Successfully executes 33,853 instructions
- Reaches EIP 0x100688 before hitting unimplemented opcode
- Current blocker: Opcode 0xFF (group opcode for INC/DEC/PUSH/JMP/CALL)

### Code Changes
- `hardemu/x86cpu.js`:
  - Removed `this.regs.eip++` after `handleInt(3)` call (line ~653)
  - Fixed ROL CF/OF flag calculation (lines ~1330-1343)
  - Added MOV r8, imm8 (0xB0-0xB7) handlers
  - Added arithmetic with immediate (0x80, 0x81, 0x83) handler
  - Added PUSHAD/POPAD (0x60, 0x61) handlers
  - Added LOOP instructions (0xE0, 0xE1, 0xE2) handlers
  - Added ADD EAX, imm32 (0x05) handler
  - Added AND instructions (0x20-0x25) handlers with handleAndRegMem()
  - Added OR/AND/SUB/XOR/CMP EAX, imm32 (0x0d, 0x25, 0x2d, 0x35, 0x3d) handlers
  - Added ADC instructions (0x10-0x13) handlers with handleAdcRegMem() and handleAdcRegMem8()

- `test-cpu5.js`:
  - Fixed ModR/M byte in ROL test: 0xE0 → 0xC0

### Current Status

The emulator successfully boots the kernel and executes 33,853 instructions before hitting an unimplemented opcode:

**Last error:**
```
Unhandled opcode: 0xff at EIP=0x100688
CPU halted after 33853 instructions
```

**Final CPU state:**
- EAX: 0x-7feaa000 (negative when interpreted as signed)
- EBX: 0x13ff003
- ECX: 0x0
- EDX: 0x156000
- ESI: 0x0
- EDI: 0x15c000
- EBP: 0x40000000
- ESP: 0x1557ec
- EIP: 0x10068a
- EFLAGS: 0x42
- CR0: 0x1
- CR3: 0x0

### Completed (This Session)

1. **Implemented opcode 0xFF (Group 5 instructions)**
   - INC r/m32 (reg field = 0)
   - DEC r/m32 (reg field = 1)
   - PUSH r/m32 (reg field = 6)
   - JMP near [r/m32] (reg field = 4)
   - CALL near [r/m32] (reg field = 2) - was already implemented
   - Status: IMPLEMENTED

2. **Fixed MOV CR0, EAX (two-byte opcode 0x0F 0x22)**
   - Bug: `handleMovCR()` was incrementing EIP twice (executeInstruction already advanced EIP to ModRM)
   - Fix: Removed extra `this.regs.eip++` at start of `handleMovCR()`
   - Same fix applied to `handleLgdtLidt()`
   - Status: FIXED

3. **Fixed translateAddress() page table walk**
   - Bug: Line 306 used `ptAddr` instead of `pteAddr` (typo)
   - Fix: Changed `this.mem.read32(ptAddr)` to `this.mem.read32(pteAddr)`
   - Also removed duplicate code block (lines 322-334) that was left from failed patch
   - Status: FIXED

4. **Implemented TEST instructions (0x84/0x85)**
   - TEST r/m8, r8 (0x84) and TEST r/m32, r32 (0x85)
   - ANDs operands, sets flags (ZF, SF, PF), clears CF/OF
   - Does NOT store result
   - Status: IMPLEMENTED

5. **Added debug output**
   - Added debug to `translateAddress()` to trace page table walks
   - Added debug to `triggerException()` to show EIP and CR2
   - Status: DONE

### Current Status

The emulator successfully:
- Executes MOV CR0, EAX (enables paging, CR0=0x80000001)
- Executes MOV EAX, 0xc010068a (loads jump target)
- Executes JMP EAX (jumps to virtual address)
- Gets to EIP=0xc01006ac (virtual address in kernel space)
- Executed 33,860 instructions (up from 33,853 before)

**Current blocker:**
- After enabling paging, CPU tries to access virtual addresses 0x8f000052 and 0x177ff088
- These aren't mapped in the page tables → #PF (Exception 14)
- IDT not set up yet → can't dispatch #PF handler
- CPU halts with "Exception 14 but IDT not set up!"

**Final CPU state:**
- EAX: 0xd88e0010
- EBX: 0x13ff003
- ECX: 0x0
- EDX: 0x156000
- ESI: 0x0
- EDI: 0x15c000
- EBP: 0x0
- ESP: 0x1557ec
- EIP: 0xc01006ac
- EFLAGS: 0x42
- CR0: 0x80000001 (paging enabled)
- CR3: 0x156000 (page directory base)

### Next Session TODO

1. **Fix page table setup**
   - The kernel loads CR3=0x156000, but page tables at that address may not be set up correctly
   - Need to trace kernel code to see how it initializes page tables
   - Or increase emulator memory and ensure identity mapping for kernel code

2. **Set up IDT before enabling paging**
   - The kernel enables paging before setting up IDT
   - Need to either:
     a. Modify kernel to set up IDT first
     b. Set up a basic IDT in the test environment
     c. Implement graceful #PF handling (print error and halt)

3. **Continue implementing missing opcodes**
   - Once paging works, kernel will likely hit more unimplemented opcodes
   - Use `ndisasm -b 32 kernel.bin | awk '{print $2}' | sort | uniq -c | sort -rn` to find missing opcodes
   - Implement in batches for efficiency

4. **Test with larger memory**
   - Currently using 256MB RAM
   - Kernel tries to access addresses >512MB (0x177ff088)
   - May need to increase memory size or fix page table mappings

### Session Learnings

- **Two-byte opcodes (0x0F prefix)**: `executeInstruction()` already advances EIP to ModRM byte. Extended instruction handlers (like `handleMovCR()`) should NOT increment EIP before reading ModRM.
- **Page table walk debugging**: Added debug output to `translateAddress()` to trace PD/PT lookups. This helped identify that PDE/PTE not present was causing #PF.
- **TEST instruction**: Doesn't store result, just sets flags. CF and OF are cleared (not like AND which preserves CF).
- **Group opcode 0xFF**: Uses ModRM.reg field to determine operation (INC=0, DEC=1, CALL=2, JMP=4, PUSH=6). Need to handle both register and memory operands.
- **EIP management**: Be very careful about when EIP is incremented. The main `step()` function reads opcode and increments EIP. Then `executeInstruction()` may increment further. Extended opcode handlers should NOT increment again for the ModRM byte.
2. **After 0xFF, expect more unimplemented opcodes**
   - Run test-kernel.js again to find next missing opcode
   - Implement in batches for efficiency

3. **Potential next opcodes (from kernel disassembly):**
   - 0xAC: LODSB (Load string byte)
   - 0xAA: STOSB (Store string byte)
   - 0x84: TEST r/m8, r8
   - 0x85: TEST r/m32, r32
   - 0xC3: RET (near return)
   - 0xC2: RET imm16 (near return with immediate pop)

4. **Long-term goals:**
   - Get kernel to fully execute without unimplemented opcodes
   - Implement video output (VGA text mode)
   - Implement keyboard input
   - Test userland programs (shell, elite game)

### Notes
- The x86 CPU emulator uses a sandboxed VM context to load memory.js and x86cpu.js
- Tests create isolated CPU/memory instances for each test case
- Debug output can be enabled via `cpu.debug = true`
- JavaScript bitwise operators work on 32-bit SIGNED integers, requiring `>>> 0` for unsigned operations
- ModR/M byte encoding: `11_xxx_000` = register mode, EAX, operation xxx
  - xxx=000 (0) = ROL
  - xxx=100 (4) = SHL/SAL
- **Efficiency tip**: Instead of implementing one opcode at a time, disassemble the entire kernel and implement missing opcodes in batches
- Kernel opcodes can be extracted with: `ndisasm -b 32 kernel.bin | awk '{print $2}' | sort | uniq -c | sort -rn`
- **Group opcodes** (like 0x80, 0x81, 0x83, 0xFF) use the ModRM reg field to determine the specific operation
- String instructions (LODSB, STOSB, etc.) use ESI/EDI and automatically increment/decrement based on DF flag

### Session Learnings
- Iterative opcode implementation (one at a time) is slow but ensures progress
- Batch implementation (disassemble first, then implement all missing) is more efficient
- The kernel uses string instructions (LODSB, STOSB) which aren't implemented yet
- ADC is critical for multi-precision arithmetic and is used early in the kernel
- AND/OR/XOR short forms with EAX (0x0d, 0x25, 0x35) are commonly used and should be implemented early
- Group opcodes (0xFF, 0x8F, etc.) require careful ModRM decoding to determine the actual operation
- JavaScript's sign extension for 8-bit values: `(value << 24) >> 24` converts unsigned 8-bit to signed 32-bit
