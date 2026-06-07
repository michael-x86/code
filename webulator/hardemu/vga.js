/**
 * VGA Text Mode Emulation
 * 
 * Emulates VGA text mode (80x25, 16 colors) for x86 emulator.
 * Renders to HTML canvas for web-based display.
 */

class VGATextMode {
    constructor(memory, canvas) {
        // Memory subsystem (to read VGA buffer at 0xB8000)
        this.mem = memory;
        
        // Canvas for rendering
        this.canvas = canvas;
        this.ctx = canvas ? canvas.getContext('2d') : null;
        
        // VGA text mode constants
        this.COLS = 80;
        this.ROWS = 25;
        this.BUF_SIZE = this.COLS * this.ROWS * 2;  // 4000 bytes
        this.BUF_ADDR = 0xB8000;  // Physical address of VGA text buffer
        
        // Current cursor position (set by kernel via ports 0x3D4/0x3D5)
        this.cursorX = 0;
        this.cursorY = 0;
        
        // VGA registers (simplified)
        this.crtcIndex = 0;  // CRT Controller index register (port 0x3D4)
        this.crtcRegisters = new Uint8Array(25);  // CRT Controller registers
        
        // Attribute controller state (with toggle flip-flop)
        this.attribIndex = 0;
        this.attribFF = 0;  // 0 = index mode, 1 = data mode
        this.attribRegs = new Uint8Array(21);
        
        // Sequencer state
        this.seqIndex = 0;
        this.seqRegs = new Uint8Array(5);
        
        // Graphics controller state
        this.graphicsIndex = 0;
        this.graphicsRegs = new Uint8Array(9);
        
        // Misc output register (port 0x3C2)
        this.miscOutput = 0x67;  // Default: VGA, color, 28.3 MHz
        
        // Feature control register
        this.featureCtrl = 0;
        
        // DAC (Digital-to-Analog Converter) state
        this.dacReadIndex = 0;
        this.dacWriteIndex = 0;
        this.dacState = 0;  // 0=write, 1=read
        this.dacPalette = new Uint8Array(768);  // 256 colors * 3 (R, G, B)
        
        // Initialize DAC with default VGA palette
        this.initDefaultPalette();
        
        // Font data (8x16 VGA font)
        this.font = null;
        this.loadDefaultFont();
        
        // Dirty tracking (for efficient rendering)
        this.dirty = true;
        this.dirtyRegions = [];
        
        // Callback for port I/O (to integrate with CPU)
        this.onPortWrite = null;
        this.onPortRead = null;
        
        // Initialize VGA state
        this.init();
    }
    
    // Initialize VGA to default text mode state
    init() {
        // Set CRTC registers to default 80x25 text mode
        this.crtcRegisters[0x00] = 0x5F;  // Horizontal total
        this.crtcRegisters[0x01] = 0x4F;  // Horizontal display enable end
        this.crtcRegisters[0x02] = 0x50;  // Horizontal blanking start
        this.crtcRegisters[0x03] = 0x82;  // Horizontal blanking end
        this.crtcRegisters[0x04] = 0x55;  // Horizontal sync pulse start
        this.crtcRegisters[0x05] = 0x81;  // Horizontal sync pulse end
        this.crtcRegisters[0x06] = 0xBF;  // Vertical total
        this.crtcRegisters[0x07] = 0x1F;  // Vertical display enable end
        this.crtcRegisters[0x08] = 0x00;  // Vertical blanking start
        this.crtcRegisters[0x09] = 0x4F;  // Maximum scan line
        this.crtcRegisters[0x0A] = 0x0D;  // Cursor start
        this.crtcRegisters[0x0B] = 0x0E;  // Cursor end
        this.crtcRegisters[0x0C] = 0x00;  // Start address high
        this.crtcRegisters[0x0D] = 0x00;  // Start address low
        this.crtcRegisters[0x0E] = (this.cursorX + this.cursorY * this.COLS) >> 8;    // Cursor location high
        this.crtcRegisters[0x0F] = (this.cursorX + this.cursorY * this.COLS) & 0xFF; // Cursor location low
        this.crtcRegisters[0x10] = 0x9C;  // Vertical sync pulse start
        this.crtcRegisters[0x11] = 0x8E;  // Vertical sync pulse end
        
        // Set attribute controller registers (reset flip-flop)
        this.attribIndex = 0;
        this.attribFF = 0;
        for (let i = 0; i < 16; i++) {
            this.attribRegs[i] = i;  // Default palette (0-15)
        }
        this.attribRegs[0x10] = 0x0C;  // Mode control
        this.attribRegs[0x11] = 0x00;  // Overscan color
        this.attribRegs[0x12] = 0x0F;  // Color plane enable
        this.attribRegs[0x13] = 0x08;  // Horizontal pixel panning
        this.attribRegs[0x14] = 0x00;  // Color select
        
        // Set sequencer registers
        this.seqRegs[0x00] = 0x03;  // Reset
        this.seqRegs[0x01] = 0x00;  // Clock mode
        this.seqRegs[0x02] = 0x03;  // Map mask
        this.seqRegs[0x03] = 0x00;  // Character map select
        this.seqRegs[0x04] = 0x02;  // Memory mode
        
        // Set graphics controller registers
        this.graphicsRegs[0x00] = 0x00;  // Set/reset
        this.graphicsRegs[0x01] = 0x00;  // Enable set/reset
        this.graphicsRegs[0x02] = 0x00;  // Color compare
        this.graphicsRegs[0x03] = 0x00;  // Data rotate
        this.graphicsRegs[0x04] = 0x00;  // Read map select
        this.graphicsRegs[0x05] = 0x10;  // Mode
        this.graphicsRegs[0x06] = 0x0E;  // Miscellaneous
        this.graphicsRegs[0x07] = 0x00;  // Color don't care
        this.graphicsRegs[0x08] = 0xFF;  // Bit mask
        
        // Mark entire screen as dirty
        this.dirty = true;
    }
    
    // Initialize default VGA palette (64 colors, duplicated for 256-color mode)
    initDefaultPalette() {
        // Standard VGA 64-color palette
        const defaultPalette = [
            0x00, 0x00, 0x00,   // 0: Black
            0x00, 0x00, 0x2A,   // 1: Blue
            0x00, 0x2A, 0x00,   // 2: Green
            0x00, 0x2A, 0x2A,   // 3: Cyan
            0x2A, 0x00, 0x00,   // 4: Red
            0x2A, 0x00, 0x2A,   // 5: Magenta
            0x2A, 0x15, 0x00,   // 6: Brown
            0x2A, 0x2A, 0x2A,   // 7: Light gray
            0x15, 0x15, 0x15,   // 8: Dark gray
            0x15, 0x15, 0x3F,   // 9: Light blue
            0x15, 0x3F, 0x15,   // 10: Light green
            0x15, 0x3F, 0x3F,   // 11: Light cyan
            0x3F, 0x15, 0x15,   // 12: Light red
            0x3F, 0x15, 0x3F,   // 13: Light magenta
            0x3F, 0x3F, 0x15,   // 14: Yellow
            0x3F, 0x3F, 0x3F,   // 15: White
        ];
        
        // Fill first 64 colors with default palette
        for (let i = 0; i < 64; i++) {
            this.dacPalette[i * 3 + 0] = defaultPalette[i * 3 + 0] << 2;  // R
            this.dacPalette[i * 3 + 1] = defaultPalette[i * 3 + 1] << 2;  // G
            this.dacPalette[i * 3 + 2] = defaultPalette[i * 3 + 2] << 2;  // B
        }
        
        // Duplicate for remaining 192 colors (simplified)
        for (let i = 64; i < 256; i++) {
            this.dacPalette[i * 3 + 0] = (i & 0x3F) << 2;
            this.dacPalette[i * 3 + 1] = ((i >> 2) & 0x3F) << 2;
            this.dacPalette[i * 3 + 2] = ((i >> 4) & 0x3F) << 2;
        }
    }
    
    // Load default 8x16 VGA font (simplified - just use ASCII)
    loadDefaultFont() {
        // In a real emulator, we'd load the actual VGA BIOS font
        // For now, we'll just use canvas built-in font rendering
        this.font = null;  // Use canvas font rendering
    }
    
    // Write to VGA port (called by CPU emulation on OUT instruction)
    portWrite(port, value) {
        switch (port) {
            case 0x3B4:  // Mono CRT Controller Index (not used in color mode)
            case 0x3D4:  // Color CRT Controller Index
                this.crtcIndex = value & 0x1F;  // Only 5 bits for CRTC index
                break;
                
            case 0x3B5:  // Mono CRT Controller Data
            case 0x3D5:  // Color CRT Controller Data
                this.crtcRegisters[this.crtcIndex] = value;
                
                // Update cursor position if cursor location registers were written
                if (this.crtcIndex === 0x0E || this.crtcIndex === 0x0F) {
                    const cursorLoc = (this.crtcRegisters[0x0E] << 8) | this.crtcRegisters[0x0F];
                    this.cursorX = cursorLoc % this.COLS;
                    this.cursorY = Math.floor(cursorLoc / this.COLS);
                    this.dirty = true;
                }
                break;
                
            case 0x3C0:  // Attribute Controller Index/Data
                if (this.attribFF) {
                    // Data mode: write to selected register, flip-flop stays 1
                    this.attribRegs[this.attribIndex & 0x1F] = value;
                } else {
                    // Index mode: set register index (bits 0-4), bit 5 stored as PAS
                    this.attribIndex = value;
                    this.attribFF = 1;
                }
                break;
                
            case 0x3C2:  // Misc Output Register
                this.miscOutput = value;
                break;
                
            case 0x3C4:  // Sequencer Index
                this.seqIndex = value & 0x07;
                break;
                
            case 0x3C5:  // Sequencer Data
                this.seqRegs[this.seqIndex] = value;
                break;
                
            case 0x3CE:  // Graphics Controller Index
                this.graphicsIndex = value & 0x0F;
                break;
                
            case 0x3CF:  // Graphics Controller Data
                this.graphicsRegs[this.graphicsIndex] = value;
                break;
                
            case 0x3C7:  // DAC Read Index
                this.dacReadIndex = value * 3;
                this.dacState = 1;  // Next reads will be from DAC
                break;
                
            case 0x3C8:  // DAC Write Index
                this.dacWriteIndex = value * 3;
                this.dacState = 0;  // Next writes will be to DAC
                break;
                
            case 0x3C9:  // DAC Data
                this.dacPalette[this.dacWriteIndex++] = value;
                if (this.dacWriteIndex >= 768) this.dacWriteIndex = 0;
                this.dirty = true;
                break;
                
            default:
                if (this.onPortWrite) {
                    this.onPortWrite(port, value);
                }
                break;
        }
    }
    
    // Read from VGA port (called by CPU emulation on IN instruction)
    portRead(port) {
        switch (port) {
            case 0x3B4:  // Mono CRT Controller Index
            case 0x3D4:  // Color CRT Controller Index
                return this.crtcIndex;
                
            case 0x3B5:  // Mono CRT Controller Data
            case 0x3D5:  // Color CRT Controller Data
                return this.crtcRegisters[this.crtcIndex] || 0;
                
            case 0x3C0:  // Attribute Controller Index/Data
                return this.attribIndex;
                
            case 0x3C1:  // Attribute Controller Status (bit 7 = 1 if waiting for data)
                return (this.attribIndex & 0x80) ? 0x00 : 0x80;
                
            case 0x3BA:  // Mono Input Status #1 — resets attribute flip-flop
            case 0x3DA:  // Color Input Status #1 — resets attribute flip-flop
                this.attribFF = 0;  // Reset attribute controller flip-flop
                return 0x08;  // Bit 3 = vertical blank (simplified)

            case 0x3C2:  // Input Status #0 (bit 4 = vertical retrace)
                // Simulate vertical retrace (simplified)
                return 0x00;  // Not in vblank
                
            case 0x3C4:  // Sequencer Index
                return this.seqIndex;
                
            case 0x3C5:  // Sequencer Data
                return this.seqRegs[this.seqIndex] || 0;
                
            case 0x3CE:  // Graphics Controller Index
                return this.graphicsIndex;
                
            case 0x3CF:  // Graphics Controller Data
                return this.graphicsRegs[this.graphicsIndex] || 0;
                
            case 0x3C7:  // DAC State (0 = ready, 1 = not ready)
                return this.dacState;
                
            case 0x3C9:  // DAC Data
                const val = this.dacPalette[this.dacReadIndex++] || 0;
                if (this.dacReadIndex >= 768) this.dacReadIndex = 0;
                return val;
                
            default:
                if (this.onPortRead) {
                    return this.onPortRead(port);
                }
                return 0xFF;  // Return undefined for unhandled ports
        }
    }
    
    // Render VGA text buffer to canvas
    render() {
        if (!this.ctx || !this.dirty) return;
        
        // Clear canvas
        this.ctx.fillStyle = '#000000';
        this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
        
        // Character dimensions
        const charWidth = this.canvas.width / this.COLS;
        const charHeight = this.canvas.height / this.ROWS;
        
        // Set font
        this.ctx.font = `${charHeight}px monospace`;
        this.ctx.textBaseline = 'top';
        
        // Read VGA text buffer from memory and render
        for (let row = 0; row < this.ROWS; row++) {
            for (let col = 0; col < this.COLS; col++) {
                const offset = (row * this.COLS + col) * 2;
                const charCode = this.mem.read8(this.BUF_ADDR + offset);
                const attr = this.mem.read8(this.BUF_ADDR + offset + 1);
                
                // Decode attribute byte
                const fgColor = attr & 0x0F;
                const bgColor = (attr >> 4) & 0x0F;
                
                // Get RGB colors from DAC palette
                const fgR = this.dacPalette[fgColor * 3 + 0];
                const fgG = this.dacPalette[fgColor * 3 + 1];
                const fgB = this.dacPalette[fgColor * 3 + 2];
                
                const bgR = this.dacPalette[bgColor * 3 + 0];
                const bgG = this.dacPalette[bgColor * 3 + 1];
                const bgB = this.dacPalette[bgColor * 3 + 2];
                
                // Draw background
                this.ctx.fillStyle = `rgb(${bgR}, ${bgG}, ${bgB})`;
                this.ctx.fillRect(col * charWidth, row * charHeight, charWidth, charHeight);
                
                // Draw character
                if (charCode >= 32 && charCode < 127) {
                    this.ctx.fillStyle = `rgb(${fgR}, ${fgG}, ${fgB})`;
                    this.ctx.fillText(
                        String.fromCharCode(charCode),
                        col * charWidth,
                        row * charHeight,
                        charWidth
                    );
                }
            }
        }
        
        // Draw cursor (if visible)
        const cursorStart = this.crtcRegisters[0x0A] & 0x1F;
        const cursorEnd = this.crtcRegisters[0x0B] & 0x1F;
        if (cursorStart <= cursorEnd) {
            this.ctx.fillStyle = '#FFFFFF';
            this.ctx.fillRect(
                this.cursorX * charWidth,
                this.cursorY * charHeight + (cursorStart * charHeight / 16),
                charWidth,
                (cursorEnd - cursorStart) * charHeight / 16
            );
        }
        
        this.dirty = false;
    }
    
    // Mark screen as dirty (needs re-render)
    markDirty() {
        this.dirty = true;
    }
    
    // Write character to screen at current cursor position
    writeChar(char, attr) {
        const offset = (this.cursorY * this.COLS + this.cursorX) * 2;
        this.mem.write8(this.BUF_ADDR + offset, char);
        this.mem.write8(this.BUF_ADDR + offset + 1, attr || 0x07);  // Default: white on black
        
        // Advance cursor
        this.cursorX++;
        if (this.cursorX >= this.COLS) {
            this.cursorX = 0;
            this.cursorY++;
            if (this.cursorY >= this.ROWS) {
                this.scroll();
                this.cursorY = this.ROWS - 1;
            }
        }
        
        // Update CRTC cursor position registers
        const cursorLoc = this.cursorY * this.COLS + this.cursorX;
        this.crtcRegisters[0x0E] = cursorLoc >> 8;
        this.crtcRegisters[0x0F] = cursorLoc & 0xFF;
        
        this.dirty = true;
    }
    
    // Scroll screen up by one line
    scroll() {
        // Move lines 1..24 up to 0..23
        for (let row = 1; row < this.ROWS; row++) {
            for (let col = 0; col < this.COLS; col++) {
                const srcOffset = (row * this.COLS + col) * 2;
                const dstOffset = ((row - 1) * this.COLS + col) * 2;
                const char = this.mem.read8(this.BUF_ADDR + srcOffset);
                const attr = this.mem.read8(this.BUF_ADDR + srcOffset + 1);
                this.mem.write8(this.BUF_ADDR + dstOffset, char);
                this.mem.write8(this.BUF_ADDR + dstOffset + 1, attr);
            }
        }
        
        // Clear last line
        for (let col = 0; col < this.COLS; col++) {
            const offset = ((this.ROWS - 1) * this.COLS + col) * 2;
            this.mem.write8(this.BUF_ADDR + offset, 0x20);  // Space
            this.mem.write8(this.BUF_ADDR + offset + 1, 0x07);  // Default attribute
        }
        
        this.dirty = true;
    }
    
    // Clear screen
    clear() {
        for (let i = 0; i < this.BUF_SIZE; i += 2) {
            this.mem.write8(this.BUF_ADDR + i, 0x20);  // Space
            this.mem.write8(this.BUF_ADDR + i + 1, 0x07);  // Default attribute
        }
        this.cursorX = 0;
        this.cursorY = 0;
        
        // Update CRTC cursor position registers
        this.crtcRegisters[0x0E] = 0x00;
        this.crtcRegisters[0x0F] = 0x00;
        
        this.dirty = true;
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = VGATextMode;
} else if (typeof window !== 'undefined') {
    window.VGATextMode = VGATextMode;
}
