/**
 * x86 Machine Emulation
 * 
 * Replaces Z80Machine for running x86 OS kernel.
 * Provides same interface as machine.js but for x86 architecture.
 */

class X86Machine {
    constructor() {
        // CPU and memory
        this.cpu = null;
        this.mem = null;
        this.vga = null;
        this.pic = null;
        this.pit = null;
        this.keyboard = null;
        this.ata = null;
        
        // Execution state
        this.running = false;
        this.halted = false;
        this.interval = null;
        this.tstates = 0;
        
        // Callbacks
        this.onVgaUpdate = null;
        this.onSerialOutput = null;  // For debug output via serial port
        
        // Debug mode
        this.debug = false;
        this.breakpoints = [];
        
        // Canvas for VGA rendering
        this.canvas = null;
        
        // CPU frequency (Hz) - target 10 MHz like Zeal 8-bit
        this.cpuFreq = 10000000;
        this.tstatesPerInterval = 0;
        
        // Initialize components
        this.init();
    }
    
    // Initialize all machine components
    init() {
        // Create memory subsystem (128MB RAM)
        this.mem = new X86Memory(128);
        this.mem.init();
        
        // Create VGA emulation
        this.vga = new VGATextMode(this.mem, this.canvas);
        
        // Create PIC (8259A)
        this.pic = new PIC8259A(this);
        
        // Create PIT (8253/8254)
        this.pit = new PIT8254(this);
        
        // Create keyboard controller
        this.keyboard = new PS2Keyboard(this);
        
        // Create ATA disk controller
        this.ata = new ATADisk(this.mem);
        
        // Create CPU (pass memory and PIC)
        this.cpu = new X86CPU(this.mem, this.pic);
        this.cpu.debug = this.debug;
        
        // Mark VGA dirty when memory is written at 0xB8000
        this.mem.onVgaUpdate = () => {
            this.vga.dirty = true;
        };
        
        // Map VGA memory-mapped I/O
        this.mem.mapMMIO(0xB8000, {
            size: 4000,
            read: (offset, size) => this.vga.read(offset, size),
            write: (offset, value, size) => {
                this.vga.write(offset, value, size);
            }
        });
        
        // Map VGA ports
        this.vga.onPortWrite = (port, value) => this.cpuPortWrite(port, value);
        this.vga.onPortRead = (port) => this.cpuPortRead(port);
        
        console.log('x86 Machine initialized');
    }
    
    // Reset the machine (like pressing reset button)
    reset() {
        this.stop();
        this.cpu.reset();
        this.mem.init();
        this.vga.init();
        this.pic.reset();
        this.pit.reset();
        this.keyboard.reset();
        this.ata.reset();
        
        // Load BIOS (simplified - just set reset vector)
        // In real hardware, reset vector is at 0xFFFFFFF0
        // For simplicity, we'll load a simple BIOS that boots from disk
        
        console.log('Machine reset');
    }
    
    // Load disk image (for ATA emulation)
    loadDiskImage(data) {
        this.mem.loadDiskImage(data);
        this.ata.loadDisk(data);
        console.log(`Disk image loaded: ${data.byteLength} bytes`);
    }
    
    // Load BIOS image (optional)
    loadBIOS(data) {
        const bytes = new Uint8Array(data);
        for (let i = 0; i < bytes.length; i++) {
            this.mem.bios8[i] = bytes[i];
        }
        console.log(`BIOS loaded: ${bytes.length} bytes`);
    }
    
    // Start execution
    start() {
        if (this.running) return;
        
        this.running = true;
        this.halted = false;
        
        // Calculate T-states per 16ms interval (roughly 60 FPS)
        this.tstatesPerInterval = Math.floor(this.cpuFreq / 60);
        
        // Start execution loop
        const outerThis = this;
        this.interval = setInterval(() => {
            outerThis.executionLoop();
        }, 16);  // ~60 FPS
        
        console.log('Machine started');
    }
    
    // Stop execution
    stop() {
        this.running = false;
        if (this.interval) {
            clearInterval(this.interval);
            this.interval = null;
        }
        console.log('Machine stopped');
    }
    
    // Execution loop (called every 16ms)
    executionLoop() {
        if (!this.running || this.halted) return;
        
        const startTstates = this.tstates;
        
        // Execute instructions until we've used our T-state budget
        while (this.tstates - startTstates < this.tstatesPerInterval) {
            const cycles = this.cpu.step();
            if (cycles === 0) {
                // CPU halted or error
                this.halted = true;
                break;
            }
            this.tstates += cycles;  // Add actual cycles from CPU
            
            // Handle PIT timer (decrement counters)
            this.pit.tick();
        }
        
        // Render VGA if dirty
        if (this.vga.dirty) {
            this.vga.render();
        }
    }
    
    // Step one instruction
    step() {
        if (this.halted) return;
        const cycles = this.cpu.step();
        if (cycles > 0) {
            this.tstates += cycles;
            this.pit.tick();
            
            // Render VGA if dirty
            if (this.vga.dirty) {
                this.vga.render();
            }
        }
    }
    
    // Step over current instruction (simplified - just step once)
    stepOver() {
        this.step();
    }
    
    // Continue execution (from breakpoint)
    cont() {
        this.start();
    }
    
    // Add breakpoint
    addBreakpoint(address) {
        this.breakpoints.push({ address: address, enabled: true });
        this.cpu.breakpoints.add(address);
    }
    
    // Remove breakpoint
    removeBreakpoint(address) {
        this.breakpoints = this.breakpoints.filter(bp => bp.address !== address);
        this.cpu.breakpoints.delete(address);
    }
    
    // Enable/disable breakpoint
    toggleBreakpoint(address) {
        const bp = this.breakpoints.find(bp => bp.address === address);
        if (bp) {
            bp.enabled = !bp.enabled;
            if (bp.enabled) {
                this.cpu.breakpoints.add(address);
            } else {
                this.cpu.breakpoints.delete(address);
            }
        }
    }
    
    // Handle CPU port write (OUT instruction)
    cpuPortWrite(port, value) {
        port &= 0xFFFF;  // Port is 16-bit
        
        if (this.debug) {
            console.log(`OUT 0x${port.toString(16)} = 0x${value.toString(16)}`);
        }
        
        // Route to appropriate device
        if (port >= 0x3B4 && port <= 0x3B5) {
            // Mono CRT Controller
            this.vga.portWrite(port, value);
        } else if (port >= 0x3C0 && port <= 0x3CF) {
            // VGA ports
            this.vga.portWrite(port, value);
        } else if (port >= 0x20 && port <= 0x21) {
            // Master PIC
            this.pic.writeMaster(port, value);
        } else if (port >= 0xA0 && port <= 0xA1) {
            // Slave PIC
            this.pic.writeSlave(port, value);
        } else if (port >= 0x40 && port <= 0x43) {
            // PIT
            this.pit.write(port, value);
        } else if (port >= 0x60 && port <= 0x64) {
            // Keyboard controller
            this.keyboard.write(port, value);
        } else if (port >= 0x1F0 && port <= 0x1F7) {
            // ATA primary channel
            this.ata.writePrimary(port, value);
        } else if (port >= 0x170 && port <= 0x177) {
            // ATA secondary channel
            this.ata.writeSecondary(port, value);
        } else if (port === 0x92) {
            // A20 line control
            this.handleA20(value);
        } else if (port === 0x3F8) {
            // Serial port (COM1) - for debug output
            if (this.onSerialOutput) {
                this.onSerialOutput(value);
            }
        } else {
            if (this.debug) {
                console.log(`Unhandled port write: 0x${port.toString(16)}`);
            }
        }
    }
    
    // Handle CPU port read (IN instruction)
    cpuPortRead(port) {
        port &= 0xFFFF;
        
        if (this.debug) {
            console.log(`IN 0x${port.toString(16)}`);
        }
        
        // Route to appropriate device
        if (port >= 0x3B4 && port <= 0x3B5) {
            return this.vga.portRead(port);
        } else if (port >= 0x3C0 && port <= 0x3CF) {
            return this.vga.portRead(port);
        } else if (port >= 0x20 && port <= 0x21) {
            return this.pic.readMaster(port);
        } else if (port >= 0xA0 && port <= 0xA1) {
            return this.pic.readSlave(port);
        } else if (port >= 0x40 && port <= 0x43) {
            return this.pit.read(port);
        } else if (port >= 0x60 && port <= 0x64) {
            return this.keyboard.read(port);
        } else if (port >= 0x1F0 && port <= 0x1F7) {
            return this.ata.readPrimary(port);
        } else if (port === 0x3F8) {
            // Serial port - always return 0 for now
            return 0;
        } else {
            if (this.debug) {
                console.log(`Unhandled port read: 0x${port.toString(16)}`);
            }
            return 0xFF;
        }
    }
    
    // Handle A20 line enable/disable
    handleA20(value) {
        // Bit 1 controls A20
        // For now, just log it
        if (this.debug) {
            console.log(`A20: ${value & 2 ? 'enabled' : 'disabled'}`);
        }
    }
    
    // Trigger IRQ (called by devices)
    triggerIRQ(irqNum) {
        this.pic.requestIRQ(irqNum);
    }
    
    // Set canvas for VGA rendering
    setCanvas(canvas) {
        this.canvas = canvas;
        if (this.vga) {
            this.vga.canvas = canvas;
            this.vga.ctx = canvas.getContext('2d');
        }
    }
    
    // Get CPU state (for debug UI)
    getCPUState() {
        return {
            regs: { ...this.cpu.regs },
            segregs: { ...this.cpu.segregs },
            cregs: { ...this.cpu.cregs },
            eflags: this.cpu.eflags,
            tstates: this.tstates
        };
    }
    
    // Set debug mode
    setDebug(debug) {
        this.debug = debug;
        if (this.cpu) {
            this.cpu.debug = debug;
        }
    }
    
    // Destroy machine (cleanup)
    destroy() {
        this.stop();
        this.cpu = null;
        this.mem = null;
        this.vga = null;
        this.pic = null;
        this.pit = null;
        this.keyboard = null;
        this.ata = null;
    }
}

// ============================================================
// PIC 8259A Emulation (simplified)
// ============================================================

class PIC8259A {
    constructor(machine) {
        this.machine = machine;
        
        // Master PIC state
        this.master = {
            irr: 0,   // Interrupt Request Register
            isr: 0,   // In-Service Register
            imr: 0,   // Interrupt Mask Register
            icw: 0,   // ICW state
            ocw: 0,   // OCW state
            base: 0x08  // Vector base (default 0x08)
        };
        
        // Slave PIC state (cascaded to master IR2)
        this.slave = {
            irr: 0,
            isr: 0,
            imr: 0,
            icw: 0,
            ocw: 0,
            base: 0x70  // Vector base (default 0x70)
        };
        
        this.init();
    }
    
    init() {
        // Reset PIC state
        this.master.irr = 0;
        this.master.isr = 0;
        this.master.imr = 0xFF;  // All masked by default
        this.slave.irr = 0;
        this.slave.isr = 0;
        this.slave.imr = 0xFF;
    }
    
    reset() {
        this.init();
    }
    
    // Write to master PIC
    writeMaster(port, value) {
        if (port === 0x20) {
            this.handleCommand(this.master, value);
        } else if (port === 0x21) {
            this.handleData(this.master, value);
        }
    }
    
    // Write to slave PIC
    writeSlave(port, value) {
        if (port === 0xA0) {
            this.handleCommand(this.slave, value);
        } else if (port === 0xA1) {
            this.handleData(this.slave, value);
        }
    }
    
    handleCommand(pic, value) {
        if (value & 0x10) {
            // ICW1: Initialization command
            pic.icw = 1;
            // TODO: Handle ICW2, ICW3, ICW4
        } else if (value & 0x08) {
            // OCW3: Read registers
            // TODO
        } else {
            // OCW2: EOI, rotate, etc.
            if (value & 0x20) {
                // EOI (End of Interrupt)
                pic.isr = 0;  // Simplified: clear all in-service
            }
        }
    }
    
    handleData(pic, value) {
        if (pic.icw === 1) {
            // ICW2: Vector base
            pic.base = value & 0xF8;
            pic.icw = 2;
        } else {
            // OCW1: IMR (Interrupt Mask Register)
            pic.imr = value;
        }
    }
    
    readMaster(port) {
        if (port === 0x20) {
            return pic.isr;  // TODO: Return appropriate register
        } else if (port === 0x21) {
            return pic.imr;
        }
        return 0;
    }
    
    readSlave(port) {
        if (port === 0xA0) {
            return pic.isr;
        } else if (port === 0xA1) {
            return pic.imr;
        }
        return 0;
    }
    
    // Request IRQ (called by devices)
    requestIRQ(irqNum) {
        if (irqNum < 8) {
            // Master PIC
            if (!(this.master.imr & (1 << irqNum))) {
                this.master.irr |= (1 << irqNum);
                this.checkInterrupts();
            }
        } else {
            // Slave PIC (IRQ 8-15)
            irqNum -= 8;
            if (!(this.slave.imr & (1 << irqNum))) {
                this.slave.irr |= (1 << irqNum);
                // Cascade to master IR2
                this.master.irr |= (1 << 2);
                this.checkInterrupts();
            }
        }
    }
    
    checkInterrupts() {
        // Check if any unmasked, unserviced IRQs are pending
        const masterPending = this.master.irr & ~this.master.imr & ~this.master.isr;
        if (masterPending) {
            // Find highest priority IRQ (lowest number)
            for (let i = 0; i < 8; i++) {
                if (masterPending & (1 << i)) {
                    // Trigger interrupt on CPU
                    const vector = this.master.base + i;
                    this.triggerInterrupt(vector);
                    this.master.isr |= (1 << i);
                    this.master.irr &= ~(1 << i);
                    break;
                }
            }
        }
    }
    
    triggerInterrupt(vector) {
        // TODO: Actually trigger interrupt on CPU
        // This requires calling CPU's interrupt handler
        if (this.machine.cpu && this.machine.cpu.getFlag('IF')) {
            // Interrupts enabled
            // TODO: Push EFLAGS, CS, EIP; load IDT entry
            console.log(`IRQ triggered: vector 0x${vector.toString(16)}`);
        }
    }
}

// ============================================================
// PIT 8254 Emulation (simplified)
// ============================================================

class PIT8254 {
    constructor(machine) {
        this.machine = machine;
        
        // PIT channels
        this.channels = [
            { mode: 3, count: 0, output: false },  // Channel 0 (IRQ0)
            { mode: 3, count: 0, output: false },  // Channel 1
            { mode: 3, count: 0, output: false },  // Channel 2
        ];
        
        this.init();
    }
    
    init() {
        // Reset PIT state
        for (let ch of this.channels) {
            ch.mode = 3;
            ch.count = 0;
            ch.output = false;
        }
    }
    
    reset() {
        this.init();
    }
    
    write(port, value) {
        const channel = port - 0x40;
        if (channel < 3) {
            // Write count value
            this.channels[channel].count = value;
            // TODO: Start counting, trigger IRQ0 on overflow
        }
    }
    
    read(port) {
        const channel = port - 0x40;
        if (channel < 3) {
            // Read current count (simplified)
            return this.channels[channel].count;
        }
        return 0;
    }
    
    // Tick PIT (called every instruction)
    tick() {
        // Simplified: Just trigger IRQ0 periodically
        // In real PIT, this would count down based on clock frequency
        // For now, we'll rely on the machine's timing
    }
}

// ============================================================
// PS/2 Keyboard Emulation (simplified)
// ============================================================

class PS2Keyboard {
    constructor(machine) {
        this.machine = machine;
        
        this.buffer = [];  // Scancode buffer
        this.outputFull = false;
        this.data = 0;
        
        this.init();
    }
    
    init() {
        this.buffer = [];
        this.outputFull = false;
        this.data = 0;
    }
    
    reset() {
        this.init();
    }
    
    write(port, value) {
        if (port === 0x60) {
            // Data port (write to keyboard)
            // TODO: Handle keyboard commands
        } else if (port === 0x64) {
            // Command port
            // TODO: Handle controller commands
        }
    }
    
    read(port) {
        if (port === 0x60) {
            // Read scancode from buffer
            if (this.buffer.length > 0) {
                this.data = this.buffer.shift();
                this.outputFull = (this.buffer.length > 0);
                return this.data;
            }
            return 0;
        } else if (port === 0x64) {
            // Status register
            let status = 0;
            if (this.outputFull || this.buffer.length > 0) {
                status |= 0x01;  // Output buffer full
            }
            return status;
        }
        return 0;
    }
    
    // Send scancode to system (called by UI)
    sendScancode(scancode) {
        this.buffer.push(scancode);
        this.outputFull = true;
        // Trigger IRQ1
        this.machine.triggerIRQ(1);
    }
    
    // Handle key event from UI
    handleKeyEvent(key, down) {
        // Convert key to scancode (set 1, XT)
        // This is a simplified mapping
        const scancodeMap = {
            'a': 0x1E, 'b': 0x30, 'c': 0x2E, 'd': 0x20,
            'e': 0x12, 'f': 0x21, 'g': 0x22, 'h': 0x23,
            'i': 0x17, 'j': 0x24, 'k': 0x25, 'l': 0x26,
            'm': 0x32, 'n': 0x31, 'o': 0x18, 'p': 0x19,
            'q': 0x10, 'r': 0x13, 's': 0x1F, 't': 0x14,
            'u': 0x16, 'v': 0x2F, 'w': 0x11, 'x': 0x2D,
            'y': 0x15, 'z': 0x2C,
            '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05,
            '5': 0x06, '6': 0x07, '7': 0x08, '8': 0x09,
            '9': 0x0A, '0': 0x0B,
            'Enter': 0x1C, 'Escape': 0x01, 'Backspace': 0x0E,
            'Tab': 0x0F, 'Space': 0x39,
        };
        
        let scancode = scancodeMap[key] || 0;
        if (scancode && !down) {
            scancode |= 0x80;  // Make break code (key release)
        }
        
        if (scancode) {
            this.sendScancode(scancode);
        }
    }
}

// ============================================================
// ATA Disk Emulation (simplified)
// ============================================================

class ATADisk {
    constructor(mem) {
        this.mem = mem;
        
        // Disk image
        this.disk = null;
        
        // ATA registers (simplified)
        this.regs = {
            data: 0,
            error: 0,
            features: 0,
            sectorCount: 0,
            lbaLow: 0,
            lbaMid: 0,
            lbaHigh: 0,
            driveHead: 0,
            status: 0x40,  // Ready
            command: 0
        };
        
        this.init();
    }
    
    init() {
        this.regs.status = 0x40;  // Ready
    }
    
    reset() {
        this.init();
    }
    
    loadDisk(data) {
        this.disk = new Uint8Array(data);
    }
    
    writePrimary(port, value) {
        const reg = port - 0x1F0;
        switch (reg) {
            case 0: this.regs.data = value; break;
            case 1: this.regs.features = value; break;
            case 2: this.regs.sectorCount = value; break;
            case 3: this.regs.lbaLow = value; break;
            case 4: this.regs.lbaMid = value; break;
            case 5: this.regs.lbaHigh = value; break;
            case 6: this.regs.driveHead = value; break;
            case 7:
                this.regs.command = value;
                this.handleCommand(value);
                break;
        }
    }
    
    readPrimary(port) {
        const reg = port - 0x1F0;
        switch (reg) {
            case 0: return this.regs.data;
            case 7: return this.regs.status;
            default: return 0;
        }
    }
    
    handleCommand(cmd) {
        switch (cmd) {
            case 0x20:  // Read sectors (retry)
            case 0x21:  // Read sectors (no retry)
                this.readSectors();
                break;
            case 0x30:  // Write sectors (retry)
            case 0x31:  // Write sectors (no retry)
                this.writeSectors();
                break;
            case 0xEC:  // Identify device
                this.identifyDevice();
                break;
            default:
                console.log(`Unhandled ATA command: 0x${cmd.toString(16)}`);
        }
    }
    
    readSectors() {
        // Calculate LBA
        const lba = (this.regs.lbaLow) |
                    (this.regs.lbaMid << 8) |
                    (this.regs.lbaHigh << 16) |
                    ((this.regs.driveHead & 0x0F) << 24);
        
        // Read sectors from disk image
        if (this.disk && lba * 512 < this.disk.length) {
            // TODO: Copy disk data to memory (usually via DMA or PIO)
            this.regs.status = 0x40;  // Ready
            // Trigger IRQ14
        } else {
            this.regs.status = 0x21;  // Error
        }
    }
    
    writeSectors() {
        // TODO: Implement write
        this.regs.status = 0x40;
    }
    
    identifyDevice() {
        // TODO: Return device identification data
        this.regs.status = 0x40;
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        X86Machine,
        PIC8259A,
        PIT8254,
        PS2Keyboard,
        ATADisk
    };
} else if (typeof window !== 'undefined') {
    window.X86Machine = X86Machine;
    window.PIC8259A = PIC8259A;
    window.PIT8254 = PIT8254;
    window.PS2Keyboard = PS2Keyboard;
    window.ATADisk = ATADisk;
}
