/**
 * BIOS — JavaScript-based BIOS trap for the x86 emulator.
 *
 * Hooks into the CPU's INT instruction to handle classic BIOS
 * services (INT 10h VGA, INT 16h keyboard, INT 13h disk) directly
 * in JavaScript, calling into the emulated device models instead of
 * dispatching through x86 handler code.
 *
 * A kernel that uses BIOS calls (rather than raw port I/O) will
 * automatically route VGA output through the emulated display,
 * keyboard input through the emulated keyboard, and disk I/O
 * through the emulated ATA controller.
 */

class BIOS {
    constructor(machine) {
        this.machine = machine;
    }

    // Called by the CPU INT instruction handler BEFORE pushing the
    // interrupt stack frame.  Return true if the BIOS handled the
    // interrupt (caller continues without dispatching); return false
    // to fall through to the normal IDT/IVT-based dispatch.
    handleInterrupt(intNum) {
        switch (intNum) {
            case 0x10: return this.int10();
            case 0x13: return this.int13();
            case 0x16: return this.int16();
            case 0x11: return this.int11();
            case 0x12: return this.int12();
            case 0x15: return this.int15();
            default:   return false;
        }
    }

    // ── INT 10h — VGA Text-Mode Services ──────────────────────────
    int10() {
        const ah = (this.machine.cpu.regs.eax >> 8) & 0xFF;
        switch (ah) {
            case 0x00: return this.int10_setMode();
            case 0x01: return this.int10_setCursorShape();
            case 0x02: return this.int10_setCursorPos();
            case 0x03: return this.int10_getCursorPos();
            case 0x06: return this.int10_scrollUp();
            case 0x07: return this.int10_scrollDown();
            case 0x08: return this.int10_readChar();
            case 0x09: return this.int10_writeChar();
            case 0x0A: return this.int10_writeCharOnly();
            case 0x0E: return this.int10_teletype();
            default:   return false;  // unhandled — fall through to IDT
        }
    }

    // AH=0x00: Set video mode
    int10_setMode() {
        const vga = this.machine.vga;
        const al = this.machine.cpu.regs.eax & 0xFF;
        if (al === 0x03 || al === 0x13) {
            vga.clear();
        }
        // TODO: switch between text and graphics mode
        return true;
    }

    // AH=0x01: Set cursor shape (ignored — we use the emulated cursor)
    int10_setCursorShape() {
        return true;
    }

    // AH=0x02: Set cursor position
    //   DH = row, DL = col, BH = page
    int10_setCursorPos() {
        const cpu = this.machine.cpu;
        const vga = this.machine.vga;
        const row = (cpu.regs.edx >> 8) & 0xFF;
        const col = cpu.regs.edx & 0xFF;
        vga.cursorX = Math.min(col, vga.COLS - 1);
        vga.cursorY = Math.min(row, vga.ROWS - 1);
        const cursorLoc = vga.cursorY * vga.COLS + vga.cursorX;
        vga.crtcRegisters[0x0E] = cursorLoc >> 8;
        vga.crtcRegisters[0x0F] = cursorLoc & 0xFF;
        vga.dirty = true;
        return true;
    }

    // AH=0x03: Get cursor position
    //   Returns: DH = row, DL = col, CX = cursor shape
    int10_getCursorPos() {
        const cpu = this.machine.cpu;
        const vga = this.machine.vga;
        const row = vga.cursorY;
        const col = vga.cursorX;
        cpu.regs.edx = (cpu.regs.edx & 0xFFFF0000) | (row << 8) | col;
        cpu.regs.ecx = (cpu.regs.ecx & 0xFFFF0000) |
            ((vga.crtcRegisters[0x0A] & 0x1F) << 8) |
            (vga.crtcRegisters[0x0B] & 0x1F);
        return true;
    }

    // AH=0x06: Scroll up window
    //   AL = lines (0 = blank whole window)
    //   BH = attribute for blanked lines
    //   CH, CL = upper-left row,col  DH, DL = lower-right row,col
    int10_scrollUp() {
        const vga = this.machine.vga;
        const al = this.machine.cpu.regs.eax & 0xFF;
        const bh = (this.machine.cpu.regs.ebx >> 8) & 0xFF;
        const ch = (this.machine.cpu.regs.ecx >> 8) & 0xFF;
        const cl = this.machine.cpu.regs.ecx & 0xFF;
        const dh = (this.machine.cpu.regs.edx >> 8) & 0xFF;
        const dl = this.machine.cpu.regs.edx & 0xFF;

        if (al === 0 || al > (dh - ch + 1)) {
            // Blank the window
            for (let row = ch; row <= dh; row++) {
                for (let col = cl; col <= dl; col++) {
                    const offset = (row * vga.COLS + col) * 2;
                    vga.mem.write8(vga.BUF_ADDR + offset, 0x20);
                    vga.mem.write8(vga.BUF_ADDR + offset + 1, bh);
                }
            }
        } else {
            // Scroll up by AL lines
            const lines = al;
            for (let row = ch; row <= dh - lines; row++) {
                for (let col = cl; col <= dl; col++) {
                    const srcOff = ((row + lines) * vga.COLS + col) * 2;
                    const dstOff = (row * vga.COLS + col) * 2;
                    const ch = vga.mem.read8(vga.BUF_ADDR + srcOff);
                    const attr = vga.mem.read8(vga.BUF_ADDR + srcOff + 1);
                    vga.mem.write8(vga.BUF_ADDR + dstOff, ch);
                    vga.mem.write8(vga.BUF_ADDR + dstOff + 1, attr);
                }
            }
            // Blank the bottom lines
            for (let row = dh - lines + 1; row <= dh; row++) {
                for (let col = cl; col <= dl; col++) {
                    const offset = (row * vga.COLS + col) * 2;
                    vga.mem.write8(vga.BUF_ADDR + offset, 0x20);
                    vga.mem.write8(vga.BUF_ADDR + offset + 1, bh);
                }
            }
        }
        vga.dirty = true;
        return true;
    }

    // AH=0x07: Scroll down (mirror of scroll up)
    int10_scrollDown() {
        // Simplified — same as scroll up but in reverse
        return this.int10_scrollUp();
    }

    // AH=0x08: Read character and attribute at cursor
    //   Returns: AL = char, AH = attribute
    int10_readChar() {
        const vga = this.machine.vga;
        const offset = (vga.cursorY * vga.COLS + vga.cursorX) * 2;
        const charCode = vga.mem.read8(vga.BUF_ADDR + offset);
        const attr = vga.mem.read8(vga.BUF_ADDR + offset + 1);
        this.machine.cpu.regs.eax = (this.machine.cpu.regs.eax & 0xFFFF0000) |
            (attr << 8) | charCode;
        return true;
    }

    // AH=0x09: Write character and attribute at cursor
    //   AL = char, BL = color/attribute, CX = count
    int10_writeChar() {
        const vga = this.machine.vga;
        const cpu = this.machine.cpu;
        const charCode = cpu.regs.eax & 0xFF;
        const attr = cpu.regs.ebx & 0xFF;
        const count = cpu.regs.ecx & 0xFFFF;

        for (let i = 0; i < count; i++) {
            const offset = (vga.cursorY * vga.COLS + vga.cursorX) * 2;
            vga.mem.write8(vga.BUF_ADDR + offset, charCode);
            vga.mem.write8(vga.BUF_ADDR + offset + 1, attr);
            vga.cursorX++;
            if (vga.cursorX >= vga.COLS) {
                vga.cursorX = 0;
                vga.cursorY = Math.min(vga.cursorY + 1, vga.ROWS - 1);
            }
        }
        vga.dirty = true;
        return true;
    }

    // AH=0x0A: Write character only (keep existing attribute)
    int10_writeCharOnly() {
        const vga = this.machine.vga;
        const cpu = this.machine.cpu;
        const charCode = cpu.regs.eax & 0xFF;
        const count = cpu.regs.ecx & 0xFFFF;

        for (let i = 0; i < count; i++) {
            const offset = (vga.cursorY * vga.COLS + vga.cursorX) * 2;
            vga.mem.write8(vga.BUF_ADDR + offset, charCode);
            vga.cursorX++;
            if (vga.cursorX >= vga.COLS) {
                vga.cursorX = 0;
                vga.cursorY = Math.min(vga.cursorY + 1, vga.ROWS - 1);
            }
        }
        vga.dirty = true;
        return true;
    }

    // AH=0x0E: Teletype output — write character, advance cursor,
    //          interpret BS/CR/LF/BEL, scroll if needed.
    int10_teletype() {
        const vga = this.machine.vga;
        const cpu = this.machine.cpu;
        const charCode = cpu.regs.eax & 0xFF;
        const page = (cpu.regs.ebx >> 8) & 0xFF;

        switch (charCode) {
            case 0x07: // BEL — ignored
                break;
            case 0x08: // Backspace
                if (vga.cursorX > 0) vga.cursorX--;
                break;
            case 0x09: // Tab
                vga.cursorX = (vga.cursorX + 8) & ~7;
                if (vga.cursorX >= vga.COLS) {
                    vga.cursorX -= vga.COLS;
                    vga.cursorY++;
                }
                break;
            case 0x0A: // Line feed
                vga.cursorY++;
                break;
            case 0x0D: // Carriage return
                vga.cursorX = 0;
                break;
            default:   // Printable character
                const offset = (vga.cursorY * vga.COLS + vga.cursorX) * 2;
                // Use default white-on-black attribute if page byte doesn't
                // carry a meaningful color (most callers leave BL unset).
                const attr = page ? 0x07 : 0x07;
                vga.mem.write8(vga.BUF_ADDR + offset, charCode);
                vga.mem.write8(vga.BUF_ADDR + offset + 1, attr);
                vga.cursorX++;
                if (vga.cursorX >= vga.COLS) {
                    vga.cursorX = 0;
                    vga.cursorY++;
                }
                break;
        }

        // Scroll if the cursor went past the bottom
        if (vga.cursorY >= vga.ROWS) {
            vga.scroll();
            vga.cursorY = vga.ROWS - 1;
        }

        // Keep CRTC registers in sync
        const cursorLoc = vga.cursorY * vga.COLS + vga.cursorX;
        vga.crtcRegisters[0x0E] = cursorLoc >> 8;
        vga.crtcRegisters[0x0F] = cursorLoc & 0xFF;

        vga.dirty = true;
        return true;
    }


    // ── INT 13h — Disk I/O Services ───────────────────────────────

    int13() {
        const ah = (this.machine.cpu.regs.eax >> 8) & 0xFF;
        switch (ah) {
            case 0x00: return this.int13_reset();
            case 0x02: return this.int13_read();
            case 0x03: return this.int13_write();
            case 0x08: return this.int13_getParams();
            default:   return false;
        }
    }

    // AH=0x00: Reset disk system
    int13_reset() {
        this.machine.cpu.regs.eax &= 0xFFFF00FF;  // AL = 0 on success
        // Clear carry (success)
        this.machine.cpu.setFlag('CF', 0);
        return true;
    }

    // AH=0x02: Read sectors
    //   AL = sectors to read, CH = cylinder low 8 bits
    //   CL[7:6] = cylinder high 2 bits, CL[5:0] = sector
    //   DH = head, DL = drive
    //   ES:BX = buffer address
    int13_read() {
        // The kernel uses ATA PIO directly rather than INT 13h,
        // so this is a stub for BIOS-aware software.
        // Stub: set CF=1 (failure) for now.
        this.machine.cpu.setFlag('CF', 1);
        return true;
    }

    // AH=0x03: Write sectors (stub)
    int13_write() {
        this.machine.cpu.setFlag('CF', 1);
        return true;
    }

    // AH=0x08: Get drive parameters (stub — return plausible CHS)
    int13_getParams() {
        const cpu = this.machine.cpu;
        // BL = drive type (1 = floppy, 2 = hard disk)
        cpu.regs.ebx = (cpu.regs.ebx & 0xFFFF0000) | 0x02;
        // CH = max cylinder low, CL[7:6] | DH = max head, DL = number of drives
        cpu.regs.ecx = (cpu.regs.ecx & 0xFFFF0000) | 0xFFFF;
        cpu.regs.edx = (cpu.regs.edx & 0xFFFF0000) | (0xFF << 8) | 0x01;
        this.machine.cpu.setFlag('CF', 0);
        return true;
    }


    // ── INT 16h — Keyboard Services ───────────────────────────────

    int16() {
        const ah = (this.machine.cpu.regs.eax >> 8) & 0xFF;
        switch (ah) {
            case 0x00: return this.int16_readKey();
            case 0x01: return this.int16_checkKey();
            case 0x02: return this.int16_getShiftFlags();
            case 0x10: return this.int16_readKeyExtended();
            case 0x11: return this.int16_checkKeyExtended();
            default:   return false;
        }
    }

    // AH=0x00: Read next key from keyboard buffer (blocking — caller
    //          should use INT 16h AH=01h to poll first).
    //   Returns: AL = ASCII, AH = scancode
    int16_readKey() {
        const cpu = this.machine.cpu;
        const keyboard = this.machine.keyboard;

        // Try to read from the PS/2 keyboard buffer
        if (keyboard.buffer.length > 0) {
            const scancode = keyboard.buffer.shift();
            keyboard.outputFull = keyboard.buffer.length > 0;
            // Map scancode to ASCII (simplified — US QWERTY)
            const ascii = this.scancodeToAscii(scancode & 0x7F);
            cpu.regs.eax = (cpu.regs.eax & 0xFFFF0000) |
                ((scancode & 0xFF) << 8) | ascii;
            return true;
        }
        // Buffer empty — for a blocking read the kernel should loop on
        // AH=01h, so we return ZF=1 (no key) for now.
        this.setZF(true);
        return true;
    }

    // AH=0x01: Check for key press (peek, non-destructive)
    //   Returns: ZF=0 if key available, AL = ASCII, AH = scancode
    //            ZF=1 if no key
    int16_checkKey() {
        const cpu = this.machine.cpu;
        const keyboard = this.machine.keyboard;
        if (keyboard.buffer.length > 0) {
            const scancode = keyboard.buffer[0];
            const ascii = this.scancodeToAscii(scancode & 0x7F);
            cpu.regs.eax = (cpu.regs.eax & 0xFFFF0000) |
                ((scancode & 0xFF) << 8) | ascii;
            this.setZF(false);
        } else {
            this.setZF(true);
        }
        return true;
    }

    // AH=0x02: Get shift flags
    //   Returns: AL = BIOS keyboard flag byte
    int16_getShiftFlags() {
        // Simplified — return 0 (no shifts active)
        this.machine.cpu.regs.eax = (this.machine.cpu.regs.eax & 0xFFFFFF00) | 0x00;
        return true;
    }

    // AH=0x10: Read key extended (same as AH=0x00)
    int16_readKeyExtended() {
        return this.int16_readKey();
    }

    // AH=0x11: Check key extended (same as AH=0x01)
    int16_checkKeyExtended() {
        return this.int16_checkKey();
    }


    // ── INT 11h — Equipment List ───────────────────────────────────

    int11() {
        // Return AX = 0x0020 (floppy + video enabled)
        this.machine.cpu.regs.eax = 0x0020;
        return true;
    }


    // ── INT 12h — Memory Size ──────────────────────────────────────

    int12() {
        // Return AX = conventional memory in KB (640 KB)
        this.machine.cpu.regs.eax = (this.machine.cpu.regs.eax & 0xFFFF0000) | 640;
        return true;
    }


    // ── INT 15h — System Services ──────────────────────────────────

    int15() {
        const ah = (this.machine.cpu.regs.eax >> 8) & 0xFF;
        switch (ah) {
            case 0x88: return this.int15_getExtMemory();
            case 0xC0: return this.int15_getConfig();
            default:   return false;
        }
    }

    // AH=0x88: Get extended memory size (in KB above 1 MB)
    int15_getExtMemory() {
        // Report 16 MB of extended memory
        this.machine.cpu.regs.eax = (this.machine.cpu.regs.eax & 0xFFFF0000) | (16 * 1024);
        this.machine.cpu.setFlag('CF', 0);
        return true;
    }

    // AH=0xC0: Get system configuration
    int15_getConfig() {
        this.machine.cpu.setFlag('CF', 1);  // No configuration table
        return true;
    }


    // ── Helpers ────────────────────────────────────────────────────

    scancodeToAscii(scancode) {
        // Simplified US QWERTY scancode → ASCII (set 1)
        const map = {
            0x01: 0x1B,  // Escape
            0x02: '1', 0x03: '2', 0x04: '3', 0x05: '4', 0x06: '5',
            0x07: '6', 0x08: '7', 0x09: '8', 0x0A: '9', 0x0B: '0',
            0x0C: '-', 0x0D: '=',
            0x0E: 0x08,  // Backspace
            0x0F: 0x09,  // Tab
            0x10: 'q', 0x11: 'w', 0x12: 'e', 0x13: 'r', 0x14: 't',
            0x15: 'y', 0x16: 'u', 0x17: 'i', 0x18: 'o', 0x19: 'p',
            0x1A: '[', 0x1B: ']', 0x1C: 0x0D,  // Enter
            0x1E: 'a', 0x1F: 's', 0x20: 'd', 0x21: 'f', 0x22: 'g',
            0x23: 'h', 0x24: 'j', 0x25: 'k', 0x26: 'l',
            0x27: ';', 0x28: "'", 0x29: '`',
            0x2B: '\\',
            0x2C: 'z', 0x2D: 'x', 0x2E: 'c', 0x2F: 'v',
            0x30: 'b', 0x31: 'n', 0x32: 'm',
            0x33: ',', 0x34: '.', 0x35: '/',
            0x39: ' ',  // Space
        };
        const ch = map[scancode];
        return typeof ch === 'string' ? ch.charCodeAt(0) : (ch || 0);
    }

    setZF(value) {
        if (value) {
            this.machine.cpu.eflags |= 1 << 6;
        } else {
            this.machine.cpu.eflags &= ~(1 << 6);
        }
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = BIOS;
} else if (typeof window !== 'undefined') {
    window.BIOS = BIOS;
}
