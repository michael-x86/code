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
        this.onFrame = null;         // Called at end of each execution frame (for UI refresh)
        
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
        this.cpu.machine = this;
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
        if (!this.running) return;
        
        const startTstates = this.tstates;
        
        // Execute instructions until we've used our T-state budget
        while (this.tstates - startTstates < this.tstatesPerInterval) {
            // Always tick PIT even when CPU is halted (may wake CPU via IRQ)
            this.pit.tick();
            
            if (this.cpu.halted) {
                // CPU is halted — let step() check for pending IRQs that may wake it
                const cycles = this.cpu.step();
                if (cycles > 0) {
                    this.tstates += cycles;
                } else {
                    this.tstates++;
                }
                continue;
            }
            
            const cycles = this.cpu.step();
            if (cycles > 0) {
                this.tstates += cycles;
            }
        }
        
        // Render VGA if dirty
        if (this.vga.dirty) {
            this.vga.render();
        }
        
        // Notify UI for refresh (register display, etc.)
        if (this.onFrame) {
            this.onFrame();
        }
    }
    
    // Step one instruction
    step() {
        this.halted = false;
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
        } else if (port >= 0x3D4 && port <= 0x3D5) {
            // Color CRT Controller
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
        } else if (port >= 0x3D4 && port <= 0x3D5) {
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
            // Serial port data register
            return 0;
        } else if (port === 0x3FD) {
            // Serial port line status register: THR empty + line ready
            return 0x60;
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
            base: 0x08,  // Vector base (default 0x08)
            readMode: 0,  // 0=IRR, 1=ISR
            icw4Needed: true,
            single: true,
            interval4: false,
            specialMask: false,
            priority: 0  // Lowest-priority IRQ (default 0 = IRQ0 highest)
        };
        
        // Slave PIC state (cascaded to master IR2)
        this.slave = {
            irr: 0,
            isr: 0,
            imr: 0,
            icw: 0,
            ocw: 0,
            base: 0x70,  // Vector base (default 0x70)
            readMode: 0,
            icw4Needed: true,
            single: true,
            interval4: false,
            specialMask: false,
            priority: 0
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
            pic.ocw = 0;
            // ICW1 indicates edge-triggered (bit 3=1 means level-triggered)
            // Bit 0: ICW4 needed
            // Bit 1: Single/cascade
            // Bit 2: Address interval (4/8)
            pic.icw4Needed = !!(value & 1);
            pic.single = !!(value & 2);
            pic.interval4 = !!(value & 4);
            // Reset ISR/IRR on initialization
            pic.isr = 0;
            pic.irr = 0;
            // Default read mode: IRR
            pic.readMode = 0;  // 0 = IRR, 1 = ISR
        } else if (value & 0x08) {
            // OCW3: Read registers / special mask / polling
            if (value & 0x01) {
                // Bit 0 selects read register: 0 = IRR, 1 = ISR
                pic.readMode = (value >> 1) & 1;
            }
            if (value & 0x02) {
                // Polling command
            }
            if (value & 0x40) {
                // Special mask mode
                pic.specialMask = !!(value & 0x10);
            }
        } else {
            // OCW2: EOI, rotate, etc.
            if (value & 0x20) {
                // EOI (End of Interrupt)
                const eoiLevel = value & 0x07;
                if (value & 0x80) {
                    // Rotate modes
                    if (value & 0x40) {
                        // Rotate on specific EOI
                        pic.isr &= ~(1 << eoiLevel);
                        pic.priority = eoiLevel;
                    } else {
                        // Rotate on automatic EOI
                        pic.priority = (pic.priority + 1) & 7;
                    }
                } else if (value & 0x40) {
                    // Specific EOI
                    pic.isr &= ~(1 << eoiLevel);
                } else {
                    // Non-specific EOI
                    pic.isr = 0;
                }
            }
        }
    }
    
    handleData(pic, value) {
        if (pic.icw === 1) {
            // ICW2: Vector base
            pic.base = value & 0xF8;
            pic.icw = 2;
        } else if (pic.icw === 2) {
            // ICW3: Cascade mapping (master: which IRQs have slaves, slave: slave ID)
            if (pic.icw3 === undefined) pic.icw3 = 0;
            pic.icw3 = value;
            pic.icw = pic.icw4Needed ? 3 : 0;
        } else if (pic.icw === 3) {
            // ICW4: Mode control
            // Bit 0: 8086 mode (1) or 8080 mode (0)
            // Bit 1: Auto EOI
            // Bit 3: Buffered mode
            // Bit 4: Special fully nested mode
            pic.icw = 0;
        } else {
            // OCW1: IMR (Interrupt Mask Register)
            pic.imr = value;
        }
    }
    
    readMaster(port) {
        if (port === 0x20) {
            if (this.master.readMode) {
                return this.master.isr;
            } else {
                return this.master.irr;
            }
        } else if (port === 0x21) {
            return this.master.imr;
        }
        return 0;
    }
    
    readSlave(port) {
        if (port === 0xA0) {
            if (this.slave.readMode) {
                return this.slave.isr;
            } else {
                return this.slave.irr;
            }
        } else if (port === 0xA1) {
            return this.slave.imr;
        }
        return 0;
    }
    
    // Request IRQ (called by devices)
    requestIRQ(irqNum) {
        if (irqNum < 8) {
            this.master.irr |= (1 << irqNum);
            this.checkInterrupts();
        } else {
            // Slave PIC (IRQ 8-15)
            irqNum -= 8;
            this.slave.irr |= (1 << irqNum);
            this.master.irr |= (1 << 2);
            this.checkInterrupts();
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
        if (this.machine.cpu && this.machine.cpu.getFlag('IF')) {
            this.machine.cpu.handleInt(vector);
        }
    }
}

// ============================================================
// PIT 8254 Emulation
// ============================================================

class PIT8254 {
    constructor(machine) {
        this.machine = machine;
        
        // PIT channels: ch0 (IRQ0), ch1, ch2
        this.channels = [
            { mode: 3, count: 0, reload: 0, ticking: false },
            { mode: 3, count: 0, reload: 0, ticking: false },
            { mode: 3, count: 0, reload: 0, ticking: false },
        ];
        
        // LSB/MSB access state for 16-bit counter writes
        this.access = [
            { state: 0, lsb: 0 },  // 0 = idle, 1 = wait for MSB
            { state: 0, lsb: 0 },
            { state: 0, lsb: 0 },
        ];
        
        this.init();
    }
    
    init() {
        for (let ch of this.channels) {
            ch.mode = 3;
            ch.count = 0;
            ch.reload = 0;
            ch.ticking = false;
        }
        for (let a of this.access) {
            a.state = 0;
            a.lsb = 0;
        }
    }
    
    reset() {
        this.init();
    }
    
    write(port, value) {
        if (port === 0x43) {
            // PIT control register
            const channel = (value >> 6) & 3;
            const access = (value >> 4) & 3;
            const mode = (value >> 1) & 7;
            
            if (channel === 3) return;  // Read-back command, ignore
            
            const ch = this.channels[channel];
            ch.mode = mode;
            
            // Configure 16-bit access state based on access mode
            const a = this.access[channel];
            if (access === 0) {
                // Latch count (for reading) — just reset access state
                a.state = 0;
            } else if (access === 1) {
                // LSB only
                a.state = 2;  // complete after single byte
            } else if (access === 2) {
                // MSB only
                a.state = 2;  // complete after single byte
            } else {
                // LSB then MSB
                a.state = 0;  // 0 = waiting for LSB
            }
            return;
        }
        
        const channel = port - 0x40;
        if (channel < 0 || channel > 2) return;
        
        const ch = this.channels[channel];
        const a = this.access[channel];
        
        if (a.state === 2) {
            // Single-byte access (LSB only or MSB only) — just store
            ch.reload = value;
            ch.count = value;
            ch.ticking = true;
            return;
        }
        
        if (a.state === 0) {
            // Waiting for LSB
            a.lsb = value;
            a.state = 1;
        } else {
            // Waiting for MSB
            const reload = (value << 8) | a.lsb;
            if (reload === 0) {
                // 0 means 65536 in PIT counting (max period)
                ch.reload = 65536;
            } else {
                ch.reload = reload;
            }
            ch.count = ch.reload;
            ch.ticking = true;
            a.state = 0;
        }
    }
    
    read(port) {
        if (port === 0x43) return 0;
        const channel = port - 0x40;
        if (channel >= 0 && channel <= 2) {
            return this.channels[channel].count & 0xFF;
        }
        return 0;
    }
    
    // Tick PIT (called every instruction)
    tick() {
        const ch = this.channels[0];
        if (!ch.ticking || ch.reload === 0) return;
        ch.count--;
        if (ch.count <= 0) {
            this.machine.triggerIRQ(0);
            ch.count = ch.reload;
        }
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
        this.enabled = true;
        this.keyboardDisabled = false;
        this.auxDisabled = false;
        this.expectedArg = 0;
        this.ctrlCmdByte = 0x47;  // Default: enable keyboard, mouse, IRQ1, IRQ12
        this.outputPort = 0x00;
        
        this.init();
    }
    
    init() {
        this.buffer = [];
        this.outputFull = false;
        this.data = 0;
        this.enabled = true;
        this.keyboardDisabled = false;
        this.auxDisabled = false;
        this.expectedArg = 0;
    }
    
    reset() {
        this.init();
    }
    
    write(port, value) {
        if (port === 0x60) {
            // Data port (write to keyboard)
            if (this.expectedArg) {
                // This is an argument for a previous command
                const cmd = this.expectedArg;
                this.expectedArg = 0;
                this.buffer.push(0xFA);  // ACK
                if (cmd === 0x60) {
                    // Write controller command byte
                    this.ctrlCmdByte = value;
                } else if (cmd === 0xD1) {
                    // Write output port
                    this.outputPort = value;
                } else if (cmd === 0xD2) {
                    // Write keyboard output buffer (echo to buffer)
                    this.buffer.push(value);
                }
                // Other commands (0xED, 0xF0, 0xF3) — argument consumed, no further action
            } else {
                this.handleKeyboardCommand(value);
            }
        } else if (port === 0x64) {
            // Command port — send command to controller
            this.handleControllerCommand(value);
        }
        this.outputFull = (this.buffer.length > 0);
    }
    
    handleKeyboardCommand(cmd) {
        switch (cmd) {
            case 0xED:  // Set LEDs
                // Next byte written to port 0x60 is LED status
                this.expectedArg = 0xED;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xEE:  // Echo
                this.buffer.push(0xEE);  // Echo response
                break;
            case 0xF0:  // Set scancode set
                this.expectedArg = 0xF0;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xF2:  // Identify keyboard
                this.buffer.push(0xFA);  // ACK
                this.buffer.push(0xAB);  // Standard PS/2 keyboard
                this.buffer.push(0x41);  // Keyboard ID byte 2
                break;
            case 0xF3:  // Set typematic rate/delay
                this.expectedArg = 0xF3;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xF4:  // Enable scanning
                this.enabled = true;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xF5:  // Disable scanning
                this.enabled = false;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xF6:  // Set default parameters
                this.enabled = true;
                this.buffer.push(0xFA);  // ACK
                break;
            case 0xFF:  // Reset
                this.enabled = true;
                this.buffer = [];
                this.buffer.push(0xFA);  // ACK
                this.buffer.push(0xAA);  // Self-test passed
                break;
            default:
                if (this.expectedArg) {
                    // Consume expected argument byte
                    this.expectedArg = 0;
                    this.buffer.push(0xFA);  // ACK
                }
                break;
        }
        this.outputFull = (this.buffer.length > 0);
    }
    
    handleControllerCommand(cmd) {
        switch (cmd) {
            case 0x20:  // Read controller command byte
                this.buffer.push(this.ctrlCmdByte);
                break;
            case 0x60:  // Write controller command byte
                this.expectedArg = 0x60;
                break;
            case 0xA7:  // Disable auxiliary device (mouse)
                this.auxDisabled = true;
                break;
            case 0xA8:  // Enable auxiliary device
                this.auxDisabled = false;
                break;
            case 0xA9:  // Test auxiliary device
                this.buffer.push(0x00);  // No error
                break;
            case 0xAA:  // Controller self-test
                this.buffer.push(0x55);  // Self-test passed
                break;
            case 0xAB:  // Test keyboard interface
                this.buffer.push(0x00);  // No error
                break;
            case 0xAD:  // Disable keyboard
                this.keyboardDisabled = true;
                break;
            case 0xAE:  // Enable keyboard
                this.keyboardDisabled = false;
                break;
            case 0xC0:  // Read input port
                this.buffer.push(0x00);
                break;
            case 0xD0:  // Read output port
                this.buffer.push(this.outputPort);
                break;
            case 0xD1:  // Write output port
                this.expectedArg = 0xD1;
                break;
            case 0xD2:  // Write keyboard output buffer
                this.expectedArg = 0xD2;
                break;
            case 0xFE:  // System board reset
                break;
            default:
                break;
        }
        this.outputFull = (this.buffer.length > 0);
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
        
        // PIO data buffer for sector reads/writes
        this.pioBuffer = new Uint16Array(256);
        this.pioOffset = 0;
        this.pioCount = 0;  // Number of words remaining in buffer
        this.pioWriteMode = false;  // true if we're expecting writes
        
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
            case 0: {
                // Data register — PIO mode
                if (this.pioWriteMode && this.pioCount > 0) {
                    // Accept write data into PIO buffer
                    this.pioBuffer[this.pioOffset] = value & 0xFFFF;
                    this.pioOffset++;
                    this.pioCount--;
                    if (this.pioCount === 0) {
                        // All data received — write to disk
                        this.pioWriteMode = false;
                        this.flushPioBuffer();
                    }
                }
                this.regs.data = value;
                break;
            }
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
            case 0: {
                // Data register — PIO mode: return next word from buffer
                if (this.pioCount > 0) {
                    const val = this.pioBuffer[this.pioOffset];
                    this.pioOffset++;
                    this.pioCount--;
                    if (this.pioCount === 0) {
                        // All data transferred
                        this.regs.status = 0x40;  // Ready
                    }
                    return val;
                }
                return 0;
            }
            case 1: return this.regs.error;
            case 2: return this.regs.sectorCount;
            case 3: return this.regs.lbaLow;
            case 4: return this.regs.lbaMid;
            case 5: return this.regs.lbaHigh;
            case 6: return this.regs.driveHead;
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
        
        const count = this.regs.sectorCount || 1;
        
        // Read sectors from disk image into PIO buffer
        if (this.disk && lba * 512 < this.disk.length) {
            const srcOffset = lba * 512;
            const wordsToRead = count * 256;
            
            for (let i = 0; i < wordsToRead && (srcOffset + i * 2 + 1) < this.disk.length; i++) {
                const lo = this.disk[srcOffset + i * 2];
                const hi = this.disk[srcOffset + i * 2 + 1];
                this.pioBuffer[i] = (hi << 8) | lo;
            }
            
            this.pioOffset = 0;
            this.pioCount = count * 256;
            this.pioWriteMode = false;
            
            this.regs.status = 0x58;  // DRDY + DRQ + SERV
        } else {
            this.regs.status = 0x21;  // Error: AMNF
        }
    }
    
    writeSectors() {
        const lba = (this.regs.lbaLow) |
                    (this.regs.lbaMid << 8) |
                    (this.regs.lbaHigh << 16) |
                    ((this.regs.driveHead & 0x0F) << 24);
        
        const count = this.regs.sectorCount || 1;
        
        if (this.disk && lba * 512 < this.disk.length) {
            // Set up PIO buffer to accept sector data
            this.pioOffset = 0;
            this.pioCount = count * 256;
            this.pioWriteMode = true;
            
            this.regs.status = 0x58;  // DRDY + DRQ + SERV
        } else {
            this.regs.status = 0x21;  // Error
        }
    }
    
    flushPioBuffer() {
        // Calculate LBA from registers
        const lba = (this.regs.lbaLow) |
                    (this.regs.lbaMid << 8) |
                    (this.regs.lbaHigh << 16) |
                    ((this.regs.driveHead & 0x0F) << 24);
        
        if (!this.disk) return;
        
        const dstOffset = lba * 512;
        for (let i = 0; i < 256 && (dstOffset + i * 2 + 1) < this.disk.length; i++) {
            const word = this.pioBuffer[i];
            this.disk[dstOffset + i * 2] = word & 0xFF;
            this.disk[dstOffset + i * 2 + 1] = (word >> 8) & 0xFF;
        }
        
        this.regs.status = 0x40;  // Ready
    }
    
    identifyDevice() {
        // Fill PIO buffer with ATA identify data
        // This is a simplified but valid identify structure
        this.pioBuffer.fill(0);
        
        // Word 0: General configuration (0x0040 = fixed disk, non-removable)
        this.pioBuffer[0] = 0x0040;
        
        // Word 1: Number of cylinders (default 16383)
        this.pioBuffer[1] = 16383;
        
        // Word 3: Number of heads (default 16)
        this.pioBuffer[3] = 16;
        
        // Word 6: Number of sectors per track (default 63)
        this.pioBuffer[6] = 63;
        
        // Words 10-19: Serial number (20 chars, ASCII)
        const serial = 'WEBULATOR00001';
        for (let i = 0; i < serial.length && i < 20; i += 2) {
            const c1 = serial.charCodeAt(i);
            const c2 = (i + 1 < serial.length) ? serial.charCodeAt(i + 1) : 0x20;
            this.pioBuffer[10 + i / 2] = (c2 << 8) | c1;
        }
        
        // Words 23-26: Firmware revision (8 chars)
        const fw = '1.00    ';
        for (let i = 0; i < fw.length && i < 8; i += 2) {
            const c1 = fw.charCodeAt(i);
            const c2 = (i + 1 < fw.length) ? fw.charCodeAt(i + 1) : 0x20;
            this.pioBuffer[23 + i / 2] = (c2 << 8) | c1;
        }
        
        // Words 27-46: Model number (40 chars)
        const model = 'Webulator Virtual Disk          ';
        for (let i = 0; i < model.length && i < 40; i += 2) {
            const c1 = model.charCodeAt(i);
            const c2 = (i + 1 < model.length) ? model.charCodeAt(i + 1) : 0x20;
            this.pioBuffer[27 + i / 2] = (c2 << 8) | c1;
        }
        
        // Word 47: Maximum PIO transfer cycle time (0x8000 = IORDY supported)
        this.pioBuffer[47] = 0x8010;
        
        // Word 49: Capabilities (0x2F00 = LBA supported, DMA, etc.)
        this.pioBuffer[49] = 0x2F00;
        
        // Word 51: PIO timing
        this.pioBuffer[51] = 0x0200;
        
        // Word 53: Valid fields (bit 0 = words 54-58 valid, bit 1 = word 64 valid)
        this.pioBuffer[53] = 0x0003;
        
        // Words 54-58: CHS values
        this.pioBuffer[54] = this.pioBuffer[1];   // Current cylinders
        this.pioBuffer[55] = this.pioBuffer[3];   // Current heads
        this.pioBuffer[56] = this.pioBuffer[6];   // Current sectors/track
        // Total sectors
        const totalSectors = this.disk ? Math.min(this.disk.length / 512, 0x0FFFFFFF) : 0;
        this.pioBuffer[57] = totalSectors & 0xFFFF;
        this.pioBuffer[58] = (totalSectors >> 16) & 0xFFFF;
        
        // Word 60-61: Total LBA sectors (28-bit)
        this.pioBuffer[60] = totalSectors & 0xFFFF;
        this.pioBuffer[61] = (totalSectors >> 16) & 0xFFFF;
        
        // Word 64: PIO modes supported
        this.pioBuffer[64] = 0x0003;  // Modes 0-3
        
        // Word 80: Major version
        this.pioBuffer[80] = 0x01F0;  // ATA/ATAPI-4
        
        // Word 81: Minor version
        this.pioBuffer[81] = 0x0000;
        
        // Word 255: Integrity signature
        this.pioBuffer[255] = 0x00A5;
        
        this.pioOffset = 0;
        this.pioCount = 256;
        this.pioWriteMode = false;
        
        this.regs.status = 0x58;  // DRDY + DRQ + SERV
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
