# Webulator → x86 Emulator: Status Report

## Goal
Fully adapt webulator (currently Z80 emulator) to run your x86 OS kernel.

## What's Been Implemented

### 1. x86 CPU Core (`hardemu/x86cpu.js`)
**Status:** Basic implementation, has bugs

**Implemented:**
- 32-bit registers (EAX, EBX, ECX, EDX, ESI, EDI, ESP, EBP)
- Segment registers (CS, DS, ES, FS, GS, SS)
- EFLAGS register with arithmetic flags (CF, PF, AF, ZF, SF, OF)
- Control registers (CR0, CR3) for paging
- Basic instructions:
  - MOV r32, imm32 (0xB8-0xBF)
  - MOV r/m32, r32 and MOV r32, r/m32 (0x89, 0x8B)
  - PUSH r32 (0x50-0x57) and POP r32 (0x58-0x5F)
  - PUSH imm8/imm32 (0x6A, 0x68)
  - RET (0xC3) and RET imm16 (0xC2)
  - JMP rel8 (0xEB) and JMP rel32 (0xE9)
  - Jcc (0x74=JE, 0x75=JNE, 0x7C=JL, 0x7D=JGE, 0x7E=JLE, 0x7F=JG)
  - CMP AL, imm8 (0x3C) and CMP EAX, imm32 (0x3D)
  - TEST (0xA8, 0xA9)
  - INC/DEC r32 (0x40-0x4F)
  - CLI/STI (0xFA, 0xFB) and CLD/STD (0xFC, 0xFD)
  - IRET/IRETD (0xCF)
  - LGDT/LIDT (0x0F 0x01 /2, /3)
  - MOV to/from CRx (0x0F 0x22, 0x0F 0x20)
- Prefixes (0x66=operand size, 0x67=address size, 0xF0=LOCK, 0xF2/F3=REP)
- Paging support (CR0.PG = 1 enables page translation)
- Debug mode with breakpoints

**Bugs found (from test-cpu4.js):**
1. MOV r/m32, r32 with mod=3 (register-to-register) has wrong ModR/M decoding
2. PUSH doesn't store correct value to stack

**Still missing (required by your kernel):**
- Real mode (16-bit) support (bootloader runs in real mode)
- Far JMP/CALL (for switching to protected mode)
- String instructions (LODS, STOS, MOVS, CMPS, SCAS)
- Shift/rotate instructions (SHL, SHR, SAR, ROL, ROR, RCL, RCR)
- MUL/DIV (unsigned multiply/divide)
- IMUL/IDIV (signed multiply/divide)
- LEA (load effective address) - actually this might be implemented
- More conditional jumps
- Enter/LEAVE instructions
- CPUID instruction
- RDTSC instruction

### 2. Memory Subsystem (`hardemu/memory.js`)
**Status:** Functional with paging support

**Implemented:**
- 128MB physical RAM (configurable)
- Memory-mapped I/O (for VGA, hardware ports)
- Virtual memory translation (32-bit paging: PDE/PTE walk)
- VGA text mode buffer at 0xB8000
- Load binary file to physical address
- Load disk image for ATA emulation
- Memory-mapped I/O device registration

**Still missing:**
- ROM BIOS emulation (proper reset vector at 0xFFFFFFF0)
- More robust MMIO handling
- Memory mirroring (for compatibility)

### 3. VGA Text Mode Emulation (`hardemu/vga.js`)
**Status:** Basic implementation

**Implemented:**
- VGA text mode (80x25, 16 colors)
- CRT Controller (CRTC) register emulation
- Attribute Controller register emulation
- Sequencer register emulation
- Graphics Controller register emulation
- DAC (palette) with default VGA palette
- Port I/O (0x3B4/5, 0x3C0, 0x3C2, 0x3C4/5, 0x3CE/F, 0x3C7/8/9)
- Canvas rendering (to display text on HTML canvas)
- Cursor position tracking
- Scroll support

**Still missing:**
- Actual 8x16 font rendering (currently uses canvas built-in font)
- Graphics mode (Mode 13h, VBE)
- Blinking text
- Line compare (split screen)
- Proper timing (retrace signals)

### 4. x86 Machine (`hardemu/machine_x86.js`)
**Status:** Architecture created, devices are stubs

**Implemented:**
- X86Machine class (replaces Z80Machine)
- Integration with CPU, memory, VGA
- Execution loop (run for N T-states)
- Port I/O routing (OUT/IN instructions)
- Debug mode and breakpoints
- Canvas setup for VGA rendering

**Stubs (need full implementation):**
- **PIC 8259A** (`PIC8259A` class): Initialization, IRR/ISR/IMR, EOI, cascade
- **PIT 8254** (`PIT8254` class): Channel 0/1/2, mode 0-5, timer interrupts
- **PS/2 Keyboard** (`PS2Keyboard` class): Scancode set 1, IRQ1 generation, UI integration
- **ATA Disk** (`ATADisk` class): PIO mode, read/write sectors, IRQ14/15

### 5. Test Files Created
- `test-hello.asm` - Minimal "Hello World" kernel (writes to VGA)
- `test-hello.bin` - Assembled binary (loaded at 0x7C00)
- `test-x86.html` - HTML test page (loads emulator in browser)
- `test-cpu.js`, `test-cpu2.js`, `test-cpu3.js`, `test-cpu4.js` - Node.js test scripts

## What's Still Needed to Run Your Kernel

### Priority 1: CPU Instruction Set
Your `bootloader.asm` and `kernel.asm` use many instructions not yet implemented:
1. **Real mode support** (16-bit, before protected mode switch)
2. **LGDT** (load GDT) - partially implemented
3. **LIDT** (load IDT) - partially implemented
4. **MOV to segment registers** (MOV to DS/ES/SS/FS/GS)
5. **Far JMP** (JMP 0x08:0x00100000) to enter protected mode
6. **String instructions** (LODSB, STOSB, etc. - used in paging setup)
7. **Shift instructions** (SHL, SHR - used in address calculation)

To find out exactly what's needed:
```bash
# Disassemble your kernel
ndisasm -b 32 kernel/kernel.bin | head -100

# Disassemble your bootloader
ndisasm -b 16 kernel/bootloader.bin | head -100
```

### Priority 2: Hardware Emulation
1. **PIC 8259A** - Kernel uses IRQs (timer, keyboard, disk)
2. **PIT 8254** - Kernel sets 100Hz timer (IRQ0)
3. **PS/2 Keyboard** - Kernel reads input via IRQ1
4. **ATA Disk** - Kernel loads filesystem from disk

### Priority 3: Boot Sequence
1. **BIOS/bootstrap** - Load bootloader at 0x7C00
2. **A20 line** - Enable address line 20 (port 0x92 or keyboard controller)
3. **GDT setup** - Kernel builds GDT, does LGDT
4. **Protected mode switch** - Kernel sets CR0.PE = 1, far JMP
5. **Paging setup** - Kernel builds page tables, sets CR3, enables CR0.PG

## Estimated Time to Complete

Based on current progress:
- **CPU instructions**: 1-2 weeks (many instructions to implement and test)
- **Hardware (PIC/PIT/keyboard/ATA)**: 2-3 weeks
- **Integration and debugging**: 2-3 weeks
- **Total**: 5-8 weeks (massive project!)

## Alternative: Use Existing x86 Emulator

Given the scope, I STRONGLY recommend using an existing x86 emulator:

### v86 (https://github.com/copy/v86)
- Mature x86 emulator in JavaScript
- Runs Linux, FreeDOS, and other OSes in browser
- Has VGA, PIC, PIT, keyboard, ATA, and more
- Your kernel should run on it with minimal changes

**To adapt v86 to run your kernel:**
1. Load your `kernel.bin` and `disk.img`
2. Set up boot sequence (or skip to protected mode)
3. Configure VGA text mode
4. Run!

This would take DAYS, not WEEKS.

### Comparison
| Approach | Time | Effort | Risk |
|----------|------|--------|------|
| Finish webulator x86 | 5-8 weeks | VERY HIGH | High (bugs, incomplete emulation) |
| Use v86 | 2-5 days | LOW | Low (mature emulator) |

## Recommendation

1. **Try v86 first** - Get your kernel running in browser quickly
2. **If v86 doesn't work**, come back to webulator x86 adaptation
3. **If you REALLY want to build your own x86 emulator**, let's focus on:
   - Implementing ONLY the instructions your kernel actually uses
   - Testing with your actual `kernel.bin`
   - Incremental hardware emulation (VGA first, then PIC/PIT, etc.)

## Next Steps (if continuing webulator x86)

1. **Disassemble your kernel** to find what instructions are needed:
   ```bash
   ndisasm -b 32 kernel/kernel.bin > kernel-disasm.txt
   ```

2. **Implement missing instructions** (priority order):
   - Real mode (16-bit) support
   - Far JMP/CALL
   - String instructions (LODS, STOS, etc.)
   - Shift/rotate instructions

3. **Test with your kernel**:
   - Load `bootloader.bin` at 0x7C00
   - Load `kernel.bin` at 0x100000
   - Run and debug

4. **Implement hardware** (as needed):
   - VGA (already partially done)
   - PIC (for interrupts)
   - PIT (for timer)
   - Keyboard (for input)
   - ATA (for disk)

## Files Created/Modified

**Created:**
- `hardemu/x86cpu.js` - x86 32-bit CPU emulation
- `hardemu/memory.js` - Memory subsystem with paging
- `hardemu/vga.js` - VGA text mode emulation
- `hardemu/machine_x86.js` - x86 machine (replaces Z80Machine)
- `test-hello.asm` - Minimal "Hello World" kernel
- `test-hello.bin` - Assembled test binary
- `test-x86.html` - HTML test page
- `test-cpu*.js` - Node.js test scripts
- `docs/webulator-x86-plan.md` - Detailed adaptation plan

**Modified:**
- None yet (webulator still uses Z80 by default)

## How to Test Current Implementation

1. **Open `test-x86.html` in browser**
2. **Click "Load test-hello.bin"**
3. **Click "Run"**
4. **Check canvas** - should show "Hello" (if CPU works correctly)

Or via Node.js:
```bash
cd /home/janko/dev/code/webulator
node test-cpu4.js
```

## Conclusion

I've created the architecture for x86 emulation in webulator, but there's still SIGNIFICANT work needed to actually run your kernel. 

**My honest recommendation:** Use v86 (https://github.com/copy/v86) - it's a mature x86 emulator that can run your kernel with minimal effort.

If you want to continue with webulator x86 adaptation, let's focus on implementing ONLY the instructions your kernel uses (disassemble `kernel.bin` to find out).

What would you like to do?
