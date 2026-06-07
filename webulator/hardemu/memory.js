/**
 * x86 Memory Subsystem
 * 
 * Handles physical memory, memory-mapped I/O, and virtual memory (paging).
 * This is used by the x86 CPU emulator to read/write memory.
 */

class X86Memory {
    constructor(sizeMB = 128) {
        // Physical memory: 128MB default (adjustable)
        this.size = sizeMB * 1024 * 1024;
        this.ram = new ArrayBuffer(this.size);
        this.ram8 = new Uint8Array(this.ram);
        this.ram16 = new Uint16Array(this.ram);
        this.ram32 = new Uint32Array(this.ram);
        
        // Memory-mapped I/O devices
        this.mmioDevices = {};  // { addr: { read(), write(value) } }
        
        // ROM BIOS (64KB at top of memory)
        this.biosSize = 64 * 1024;
        this.bios = new ArrayBuffer(this.biosSize);
        this.bios8 = new Uint8Array(this.bios);
        
        // VRAM (VGA text mode: 0xB8000, 80x25x2 = 4000 bytes)
        this.vgaTextBase = 0xB8000;
        this.vgaTextSize = 80 * 25 * 2;  // 4000 bytes
        this.vgaText = new Uint8Array(this.vgaTextSize);
        
        // VGA mode (text or graphics)
        this.vgaMode = 'text';  // 'text' or 'graphics'
        
        // Callback for VGA updates (to update canvas)
        this.onVgaUpdate = null;
    }
    
    // Initialize memory (clear RAM, load BIOS)
    init() {
        // Clear RAM
        this.ram8.fill(0);
        
        // Clear VRAM
        this.vgaText.fill(0);
        
        // Load BIOS (reset vector at 0xFFFFFFF0)
        this.loadBIOS();
    }
    
    // Load BIOS (simplified - just set up reset vector)
    loadBIOS() {
        // Reset vector: far jump to 0xF000:0xE05B (typical BIOS entry)
        // At physical 0xFFFFFFF0:
        //   EA 5B E0 00 F0  (JMP F000:E05B)
        const resetVector = 0xFFFFFFF0 & 0xFFFFFFFF;
        // We can't write to this address directly (it's in BIOS region)
        // In a real emulator, we'd have BIOS ROM mapped here
        // For now, we'll just set CS:IP to 0xF000:0xE05B in the CPU reset
    }
    
    // Read 8-bit value from physical address
    read8(physAddr) {
        physAddr = physAddr >>> 0;  // Force unsigned 32-bit
        
        // Check VGA text memory
        if (physAddr >= this.vgaTextBase && physAddr < this.vgaTextBase + this.vgaTextSize) {
            return this.vgaText[physAddr - this.vgaTextBase];
        }
        
        // Check MMIO
        for (let deviceAddr in this.mmioDevices) {
            const device = this.mmioDevices[deviceAddr];
            const addr = Number(deviceAddr);
            if (physAddr >= addr && physAddr < addr + device.size) {
                return device.read(physAddr - addr, 1);
            }
        }
        
        // Check BIOS (top 64KB)
        if (physAddr >= this.size - this.biosSize && physAddr < this.size) {
            const biosOffset = physAddr - (this.size - this.biosSize);
            return this.bios8[biosOffset];
        }
        
        // Regular RAM
        if (physAddr < this.size) {
            return this.ram8[physAddr];
        }
        
        console.warn(`read8: Address 0x${physAddr.toString(16)} out of range`);
        return 0xFF;  // Return FF for unmapped memory
    }
    
    // Read 16-bit value from physical address
    read16(physAddr) {
        const low = this.read8(physAddr);
        const high = this.read8(physAddr + 1);
        return (high << 8) | low;
    }
    
    // Read 32-bit value from physical address
    read32(physAddr) {
        const b0 = this.read8(physAddr);
        const b1 = this.read8(physAddr + 1);
        const b2 = this.read8(physAddr + 2);
        const b3 = this.read8(physAddr + 3);
        return ((b3 << 24) | (b2 << 16) | (b1 << 8) | b0) >>> 0;
    }
    
    // Write 8-bit value to physical address
    write8(physAddr, value) {
        physAddr = physAddr >>> 0;
        value = value & 0xFF;
        
        // Check VGA text memory
        if (physAddr >= this.vgaTextBase && physAddr < this.vgaTextBase + this.vgaTextSize) {
            const offset = physAddr - this.vgaTextBase;
            this.vgaText[offset] = value;
            
            // Notify VGA update callback
            if (this.onVgaUpdate) {
                this.onVgaUpdate(offset, 1);
            }
            return;
        }
        
        // Check MMIO
        for (let deviceAddr in this.mmioDevices) {
            const device = this.mmioDevices[deviceAddr];
            const addr = Number(deviceAddr);
            if (physAddr >= addr && physAddr < addr + device.size) {
                device.write(physAddr - addr, value, 1);
                return;
            }
        }
        
        // Regular RAM
        if (physAddr < this.size) {
            this.ram8[physAddr] = value;
            return;
        }
        
        console.warn(`write8: Address 0x${physAddr.toString(16)} out of range`);
    }
    
    // Write 16-bit value to physical address
    write16(physAddr, value) {
        this.write8(physAddr, value & 0xFF);
        this.write8(physAddr + 1, (value >> 8) & 0xFF);
    }
    
    // Write 32-bit value to physical address
    write32(physAddr, value) {
        this.write8(physAddr, value & 0xFF);
        this.write8(physAddr + 1, (value >> 8) & 0xFF);
        this.write8(physAddr + 2, (value >> 16) & 0xFF);
        this.write8(physAddr + 3, (value >> 24) & 0xFF);
    }
    
    // Load a binary file into memory at specified physical address
    loadBinary(data, address) {
        const bytes = new Uint8Array(data);
        for (let i = 0; i < bytes.length; i++) {
            this.write8(address + i, bytes[i]);
        }
        console.log(`Loaded ${bytes.length} bytes at physical 0x${address.toString(16)}`);
    }
    
    // Load disk image (for ATA emulation)
    loadDiskImage(data) {
        // Store disk image for ATA emulation to use
        this.diskImage = new Uint8Array(data);
        console.log(`Loaded disk image: ${this.diskImage.length} bytes`);
    }
    
    // Map MMIO device
    mapMMIO(baseAddr, device) {
        this.mmioDevices[baseAddr] = device;
        console.log(`Mapped MMIO device at 0x${baseAddr.toString(16)}`);
    }
    
    // Unmap MMIO device
    unmapMMIO(baseAddr) {
        delete this.mmioDevices[baseAddr];
    }
    
    // Get VGA text buffer (for rendering)
    getVGATextBuffer() {
        return this.vgaText;
    }
    
    // Set VGA mode (text or graphics)
    setVGAMode(mode) {
        this.vgaMode = mode;
        if (mode === 'text') {
            // Clear graphics memory, enable text
            this.vgaText.fill(0);
        }
    }
    
    // Read memory range (for debugging)
    readRange(startAddr, length) {
        const result = [];
        for (let i = 0; i < length; i++) {
            result.push(this.read8(startAddr + i));
        }
        return result;
    }
    
    // Write memory range (for debugging)
    writeRange(startAddr, data) {
        for (let i = 0; i < data.length; i++) {
            this.write8(startAddr + i, data[i]);
        }
    }
    
    // Dump memory to console (debug)
    dump(addr, lines = 16) {
        for (let i = 0; i < lines; i++) {
            let line = `0x${(addr + i * 16).toString(16).padStart(8, '0')}: `;
            for (let j = 0; j < 16; j++) {
                line += `${this.read8(addr + i * 16 + j).toString(16).padStart(2, '0')} `;
            }
            console.log(line);
        }
    }
}

// VGA Text Mode MMIO Device (simplified)
class VGATextDevice {
    constructor(memory) {
        this.mem = memory;
        this.size = 4000;  // 80x25x2 bytes
        this.baseAddr = 0xB8000;
        
        // Cursor position (row, col)
        this.cursorRow = 0;
        this.cursorCol = 0;
    }
    
    read(offset, size) {
        // Read from VGA text buffer
        return this.mem.vgaText[offset];
    }
    
    write(offset, value, size) {
        // Write to VGA text buffer
        this.mem.vgaText[offset] = value;
        
        // Update cursor position if writing to cursor attribute
        if (offset % 2 === 1) {
            // Attribute byte - update cursor
            const charIndex = offset / 2;
            this.cursorRow = Math.floor(charIndex / 80);
            this.cursorCol = charIndex % 80;
        }
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { X86Memory, VGATextDevice };
} else if (typeof window !== 'undefined') {
    window.X86Memory = X86Memory;
    window.VGATextDevice = VGATextDevice;
}
