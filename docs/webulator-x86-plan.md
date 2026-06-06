# Webulator → x86 Emulator Adaptation Plan

## Goal
Fully adapt webulator (currently Z80 emulator for Zeal 8-bit Computer) to emulate an x86 (32-bit) PC capable of running your x86 OS kernel.

## Current State
- **Webulator**: Z80 CPU + Zeal 8-bit hardware emulation (deprecated, web-based)
- **Your x86 Kernel**: 32-bit protected mode, paging, VFS, syscalls, shell, userland

## Architecture Requirements (from kernel analysis)

### CPU Requirements
- 32-bit x86 (i386+) with protected mode
- Paging support (CR0, CR3, page tables)
- Interrupt handling (IDT, exception gates)
- Task switching (TSS, hardware task switch)

### Hardware Emulation Needed
1. **VGA Text Mode** (0xB8000, 80x25, 16 colors)
2. **PIC** (8259A, ports 0x20/0x21 & 0xA0/0xA1, IRQs 0-15)
3. **PIT** (8253/8254, ports 0x40-0x43, IRQ0 timer)
4. **PS/2 Keyboard** (ports 0x60/0x64, IRQ1, scancode set 1)
5. **ATA Disk Controller** (ports 0x1F0-0x1F7, 0x3F6, IRQ14/15)
6. **Serial Port** (0x3F8, optional debug output)
7. **A20 Line** (port 0x92 or keyboard controller)
8. **CMOS/RTC** (port 0x70/0x71, optional)

### Boot Sequence Expected
1. Bootloader loaded at 0x7C00 (real mode)
2. A20 line enabled
3. Kernel loaded from disk to physical 0x100000 (1MB)
4. GDT loaded, protected mode entered
5. Kernel entry at 0x00100000 (physical) or 0xC0100000 (virtual)

## Implementation Phases

### Phase 1: x86 CPU Core (32-bit, protected mode)
**Files to create:**
- `hardemu/x86cpu.js` - x86 CPU emulation (replaces `hardemu/Z80.js`)

**Instructions to implement (minimum set):**
- Data movement: MOV, PUSH, POP, LEA
- Arithmetic: ADD, SUB, AND, OR, XOR, CMP, TEST
- Control: JMP, CALL, RET, IRET/IRETD, Jcc (JE, JNE, JG, JL, etc.)
- Flags: STC, CLC, CMC, CLD, STD, CLI, STI
- Bit ops: SHL, SHR, SAR, ROL, ROR, RCL, RCR
- Load/store: LODS, STOS, MOVS, CMPS, SCAS
- Segments: MOV to/from segment regs, far JMP/CALL
- Control registers: MOV to/from CR0, CR3, CR4
- Paging: walk page tables, translate addresses

**Deliverable:** CPU that can execute simple 32-bit code in protected mode.

### Phase 2: Memory Subsystem
**Files to create:**
- `hardemu/memory.js` - Physical/virtual memory management

**Features:**
- 32-bit physical address space (up to 4GB, but we can limit to 128MB)
- Virtual memory translation (32-bit paging: PDE/PTE)
- Identity mapping for first 4MB (kernel's pre-paging assumption)
- Memory-mapped I/O (VGA, hardware ports)

**Deliverable:** Memory system that supports paging and MMIO.

### Phase 3: Hardware - VGA Text Mode
**Files to create:**
- `hardemu/vga.js` - VGA text mode emulation

**Features:**
- Text mode 80x25, 16 colors
- Framebuffer at 0xB8000 (physical)
- Cursor position (ports 0x3D4/0x3D5)
- Scroll support
- Integration with webulator's canvas for display

**Deliverable:** Kernel can print to screen via VGA.

### Phase 4: Hardware - PIC & PIT
**Files to create:**
- `hardemu/pic.js` - 8259A PIC emulation
- `hardemu/pit.js` - 8253/8254 PIT emulation

**PIC Features:**
- Master/slave cascade
- IRR, ISR, IMR registers
- Edge/level triggered
- EOI commands

**PIT Features:**
- Channel 0: periodic interrupt (IRQ0)
- Mode 2 (rate generator) or Mode 3 (square wave)
- Configurable frequency (kernel uses 100Hz)

**Deliverable:** Timer interrupts fire, kernel can set up scheduling.

### Phase 5: Hardware - PS/2 Keyboard
**Files to create:**
- `hardemu/keyboard.js` - PS/2 keyboard emulation

**Features:**
- Scancode set 1 (XT)
- IRQ1 generation on keypress/release
- Integration with webulator's UI (capture keyboard events)
- Modifier keys (Shift, Ctrl, Alt)

**Deliverable:** Kernel can read keyboard input via IRQ1.

### Phase 6: Hardware - ATA Disk
**Files to create:**
- `hardemu/ata.js` - ATA/IDE disk emulation

**Features:**
- PIO mode (ports 0x1F0-0x1F7, 0x3F6)
- Read/write sectors
- IRQ14/IRQ15 on operation complete
- Load disk image from file (your `disk.img`)

**Deliverable:** Kernel can load filesystem from disk.

### Phase 7: Boot Sequence & Integration
**Files to modify:**
- `hardemu/machine.js` - Main machine emulation loop
- `index.html` - UI updates

**Features:**
- Reset vector at 0xFFFFFFF0 (real mode) → 0x7C00 (bootloader)
- A20 line enable (port 0x92)
- GDT/IDT loading (LGDT, LIDT)
- Protected mode switch (CR0.PE = 1)
- Kernel loading from disk image

**Deliverable:** Full boot sequence from `bootloader.asm` to kernel entry.

### Phase 8: Testing & Debugging
**Features:**
- Debug console (show CPU state, disassembly)
- Breakpoints (software/HW)
- Memory viewer
- Register editor

**Deliverable:** Ability to debug kernel running in emulator.

## File Structure (after adaptation)

```
webulator/
├── hardemu/
│   ├── x86cpu.js          # x86 CPU (NEW)
│   ├── memory.js           # Memory + paging (NEW)
│   ├── vga.js             # VGA text mode (NEW)
│   ├── pic.js             # 8259A PIC (NEW)
│   ├── pit.js             # 8253 PIT (NEW)
│   ├── keyboard.js        # PS/2 keyboard (NEW)
│   ├── ata.js             # ATA disk (NEW)
│   ├── machine.js         # Main loop (MODIFY)
│   └── (remove Z80.js, zeal8bitcomputer.js)
├── index.html             # UI (MODIFY)
├── view/                 # UI components (MODIFY)
└── ...
```

## Immediate Next Steps

1. **Create x86 CPU core** (`hardemu/x86cpu.js`)
   - Implement basic execution loop
   - Decode x86 instructions (32-bit mode)
   - Start with MOV, ADD, SUB, JMP, CALL, RET

2. **Create memory subsystem** (`hardemu/memory.js`)
   - Physical memory array
   - Virtual → physical translation (paging)
   - MMIO region detection

3. **Test with simple code**
   - Write a small test kernel (assembly)
   - Verify CPU can execute it
   - Add VGA output for debugging

4. **Incrementally add hardware**
   - VGA → can see output
   - PIC/PIT → timer interrupts work
   - Keyboard → can type
   - ATA → can load filesystem

## Estimated Timeline

- Phase 1-2 (CPU + Memory): 2-3 weeks
- Phase 3-6 (Hardware): 3-4 weeks
- Phase 7-8 (Integration + Debug): 2-3 weeks
- **Total: 7-10 weeks (massive project)**

## Alternative Approach

Given the scope, consider:
1. **Use existing x86 emulator**: [v86](https://github.com/copy/v86) is a mature x86 emulator in JavaScript that already runs Linux. Adapting it to run your kernel might be easier.
2. **Start minimal**: Implement only the hardware your kernel actually uses (check your `kernel/src/includes/*.inc` to see what's referenced).

## Recommendation

Start with **Phase 1 (x86 CPU core)** and get a simple "Hello World" kernel running in the emulator. This will validate the CPU emulation before adding hardware.

Would you like me to start implementing Phase 1 (x86 CPU core) now?
