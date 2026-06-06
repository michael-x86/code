/**
 * x86 (32-bit) CPU Emulator
 * 
 * Implements i386+ architecture with protected mode and paging support.
 * This is the core of the webulator x86 adaptation to run the user's OS kernel.
 */

class X86CPU {
    constructor(memory, pic) {
        // 32-bit general purpose registers
        this.regs = {
            eax: 0, ebx: 0, ecx: 0, edx: 0,
            esi: 0, edi: 0, esp: 0, ebp: 0,
            eip: 0
        };
        
        // Segment registers (16-bit)
        this.segregs = {
            cs: 0, ds: 0, es: 0, fs: 0, gs: 0, ss: 0
        };
        
        // Control registers
        this.cregs = {
            cr0: 0,  // PE (bit 0), PG (bit 31)
            cr1: 0,
            cr2: 0,  // Page fault linear address
            cr3: 0   // Page directory base (top 20 bits)
        };
        
        // Debug registers (stub)
        this.dregs = { dr0: 0, dr1: 0, dr2: 0, dr3: 0, dr6: 0, dr7: 0 };
        
        // EFLAGS register
        this.eflags = 0x00000002;  // Bit 1 always set
        
        // Memory subsystem (provided by memory.js)
        this.mem = memory;
        
        // PIC for interrupt handling
        this.pic = pic;
        
        // IDT (Interrupt Descriptor Table)
        this.idtBase = 0;
        this.idtLimit = 0;
        
        // GDT (Global Descriptor Table)
        this.gdtBase = 0;
        this.gdtLimit = 0;
        
        // Current privilege level (0 = kernel, 3 = user)
        this.cpl = 0;
        
        // Halted state
        this.halted = false;
        
        // T-state tracking (for cycle-accurate emulation)
        this.tstates = 0;
        
        // Debug mode
        this.debug = false;
        this.breakpoints = new Set();
        
        // Instruction decoding state
        this.prefixes = {
            segOverride: null,  // Segment override
            operandSize: false,   // 0x66 prefix
            addressSize: false,   // 0x67 prefix
            lock: false,           // 0xF0 prefix
            rep: 0                // 0 = no rep, 1 = REPZ (F3), 2 = REPNZ (F2)
        };
    }
    
    // Reset CPU to initial state
    reset() {
        // Reset registers
        for (let reg in this.regs) {
            this.regs[reg] = 0;
        }
        this.regs.eip = 0xFFFFFFF0;  // Reset vector (ROM BIOS location)
        
        // Reset segment registers
        for (let reg in this.segregs) {
            this.segregs[reg] = 0;
        }
        this.segregs.cs = 0xF000;  // Typical BIOS segment
        
        // Reset control registers
        this.cregs.cr0 = 0;  // Real mode, no paging
        this.cregs.cr3 = 0;
        
        // Reset EFLAGS
        this.eflags = 0x00000002;
        
        // Reset state
        this.halted = false;
        this.cpl = 0;
        this.tstates = 0;
    }
    
    // Set/get 32-bit register by name
    getReg32(name) {
        return this.regs[name.toLowerCase()] || 0;
    }
    
    setReg32(name, value) {
        this.regs[name.toLowerCase()] = value >>> 0;  // Force unsigned 32-bit
    }
    
    // Get 16-bit register (lower half of 32-bit reg)
    getReg16(name) {
        const reg32 = this.getReg32(name);
        return reg32 & 0xFFFF;
    }
    
    setReg16(name, value) {
        const regName = name.toLowerCase();
        const reg32 = this.getReg32(regName);
        this.regs[regName] = (reg32 & 0xFFFF0000) | (value & 0xFFFF);
    }
    
    // Get 8-bit register (lowest byte)
    getReg8(name) {
        const regName = name.toLowerCase();
        if (regName === 'al') return this.regs.eax & 0xFF;
        if (regName === 'ah') return (this.regs.eax >> 8) & 0xFF;
        if (regName === 'bl') return this.regs.ebx & 0xFF;
        if (regName === 'bh') return (this.regs.ebx >> 8) & 0xFF;
        if (regName === 'cl') return this.regs.ecx & 0xFF;
        if (regName === 'ch') return (this.regs.ecx >> 8) & 0xFF;
        if (regName === 'dl') return this.regs.edx & 0xFF;
        if (regName === 'dh') return (this.regs.edx >> 8) & 0xFF;
        return 0;
    }
    
    setReg8(name, value) {
        const regName = name.toLowerCase();
        const val8 = value & 0xFF;
        if (regName === 'al') {
            this.regs.eax = (this.regs.eax & 0xFFFFFF00) | val8;
        } else if (regName === 'ah') {
            this.regs.eax = (this.regs.eax & 0xFFFF00FF) | (val8 << 8);
        } else if (regName === 'bl') {
            this.regs.ebx = (this.regs.ebx & 0xFFFFFF00) | val8;
        } else if (regName === 'bh') {
            this.regs.ebx = (this.regs.ebx & 0xFFFF00FF) | (val8 << 8);
        } else if (regName === 'cl') {
            this.regs.ecx = (this.regs.ecx & 0xFFFFFF00) | val8;
        } else if (regName === 'ch') {
            this.regs.ecx = (this.regs.ecx & 0xFFFF00FF) | (val8 << 8);
        } else if (regName === 'dl') {
            this.regs.edx = (this.regs.edx & 0xFFFFFF00) | val8;
        } else if (regName === 'dh') {
            this.regs.edx = (this.regs.edx & 0xFFFF00FF) | (val8 << 8);
        }
    }
    
    // EFLAGS operations
    getFlag(flag) {
        switch (flag) {
            case 'CF': return (this.eflags >> 0) & 1;   // Carry
            case 'PF': return (this.eflags >> 2) & 1;   // Parity
            case 'AF': return (this.eflags >> 4) & 1;   // Auxiliary
            case 'ZF': return (this.eflags >> 6) & 1;   // Zero
            case 'SF': return (this.eflags >> 7) & 1;   // Sign
            case 'TF': return (this.eflags >> 8) & 1;   // Trap
            case 'IF': return (this.eflags >> 9) & 1;   // Interrupt enable
            case 'DF': return (this.eflags >> 10) & 1;  // Direction
            case 'OF': return (this.eflags >> 11) & 1;  // Overflow
            case 'IOPL': return (this.eflags >> 12) & 3; // I/O privilege
            case 'NT': return (this.eflags >> 14) & 1;  // Nested task
            case 'RF': return (this.eflags >> 16) & 1;  // Resume
            case 'VM': return (this.eflags >> 17) & 1;  // Virtual 8086
            case 'AC': return (this.eflags >> 18) & 1;  // Alignment check
            case 'VIF': return (this.eflags >> 19) & 1; // Virtual interrupt
            case 'VIP': return (this.eflags >> 20) & 1; // Virtual interrupt pending
            case 'ID': return (this.eflags >> 21) & 1;  // ID flag
            default: return 0;
        }
    }
    
    setFlag(flag, value) {
        const val = value ? 1 : 0;
        switch (flag) {
            case 'CF': this.eflags = (this.eflags & ~(1 << 0)) | (val << 0); break;
            case 'PF': this.eflags = (this.eflags & ~(1 << 2)) | (val << 2); break;
            case 'AF': this.eflags = (this.eflags & ~(1 << 4)) | (val << 4); break;
            case 'ZF': this.eflags = (this.eflags & ~(1 << 6)) | (val << 6); break;
            case 'SF': this.eflags = (this.eflags & ~(1 << 7)) | (val << 7); break;
            case 'TF': this.eflags = (this.eflags & ~(1 << 8)) | (val << 8); break;
            case 'IF': this.eflags = (this.eflags & ~(1 << 9)) | (val << 9); break;
            case 'DF': this.eflags = (this.eflags & ~(1 << 10)) | (val << 10); break;
            case 'OF': this.eflags = (this.eflags & ~(1 << 11)) | (val << 11); break;
            case 'IOPL': this.eflags = (this.eflags & ~(3 << 12)) | (val << 12); break;
            case 'NT': this.eflags = (this.eflags & ~(1 << 14)) | (val << 14); break;
            case 'RF': this.eflags = (this.eflags & ~(1 << 16)) | (val << 16); break;
            case 'VM': this.eflags = (this.eflags & ~(1 << 17)) | (val << 17); break;
            case 'AC': this.eflags = (this.eflags & ~(1 << 18)) | (val << 18); break;
            case 'VIF': this.eflags = (this.eflags & ~(1 << 19)) | (val << 19); break;
            case 'VIP': this.eflags = (this.eflags & ~(1 << 20)) | (val << 20); break;
            case 'ID': this.eflags = (this.eflags & ~(1 << 21)) | (val << 21); break;
        }
    }
    
    // Update arithmetic flags for an operation
    updateArithmeticFlags(result, op1, op2, carry, isByteOp) {
        const mask = isByteOp ? 0xFF : 0xFFFFFFFF;
        const signBit = isByteOp ? 0x80 : 0x80000000;
        
        // Zero flag
        this.setFlag('ZF', (result & mask) === 0);
        
        // Sign flag
        this.setFlag('SF', (result & signBit) !== 0);
        
        // Parity flag (lowest 8 bits)
        let parity = 1;
        let byte = result & 0xFF;
        for (let i = 0; i < 8; i++) {
            parity ^= (byte >> i) & 1;
        }
        this.setFlag('PF', parity === 0);
        
        // Carry flag
        if (carry !== undefined) {
            this.setFlag('CF', carry);
        }
        
        // Overflow flag (for signed arithmetic)
        if (op1 !== undefined && op2 !== undefined) {
            const sign1 = op1 & signBit;
            const sign2 = op2 & signBit;
            const signR = result & signBit;
            // Overflow if signs of operands same but result sign different
            this.setFlag('OF', (sign1 === sign2) && (sign1 !== signR));
        }
    }
    
    // Read memory (with paging if enabled)
    readMem(addr, size) {
        let physicalAddr = addr;
        
        // Apply paging if CR0.PG = 1
        if (this.cregs.cr0 & 0x80000000) {
            physicalAddr = this.translateAddress(addr);
        }
        
        switch (size) {
            case 1: return this.mem.read8(physicalAddr);
            case 2: return this.mem.read16(physicalAddr);
            case 4: return this.mem.read32(physicalAddr);
            default: return 0;
        }
    }
    
    // Write memory (with paging if enabled)
    writeMem(addr, value, size) {
        let physicalAddr = addr;
        
        // Apply paging if CR0.PG = 1
        if (this.cregs.cr0 & 0x80000000) {
            physicalAddr = this.translateAddress(addr);
        }
        
        switch (size) {
            case 1: this.mem.write8(physicalAddr, value); break;
            case 2: this.mem.write16(physicalAddr, value); break;
            case 4: this.mem.write32(physicalAddr, value); break;
        }
    }
    
    // Translate virtual address to physical using page tables
    translateAddress(vaddr) {
        if (!(this.cregs.cr0 & 0x80000000)) {
            return vaddr;  // Paging disabled
        }
        
        const pdIndex = (vaddr >>> 22) & 0x3FF;
        const ptIndex = (vaddr >>> 12) & 0x3FF;
        const offset = vaddr & 0xFFF;
        
        const pdBase = this.cregs.cr3 & 0xFFFFF000;  // Page directory base
        const pdeAddr = pdBase + (pdIndex * 4);
        const pde = this.mem.read32(pdeAddr);
        
        if (!(pde & 1)) {
            // Page not present - page fault
            this.cregs.cr2 = vaddr;
            this.triggerException(14, 0);  // #PF
            return 0;
        }
        
        const ptBase = pde & 0xFFFFF000;
        const pteAddr = ptBase + (ptIndex * 4);
        const pte = this.mem.read32(pteAddr);
        
        if (!(pte & 1)) {
            // Page not present - page fault
            this.cregs.cr2 = vaddr;
            this.triggerException(14, 0);  // #PF
            return 0;
        }
        
        const physBase = pte & 0xFFFFF000;
        return physBase + offset;
    }
    
    // Trigger an exception
    triggerException(exceptionNum, errorCode) {
        if (this.debug) {
            console.log(`Exception ${exceptionNum} at EIP=${this.regs.eip.toString(16)}`);
        }
        
        // Check if IDT is set up
        if (this.idtLimit === 0) {
            console.error(`Exception ${exceptionNum} but IDT not set up!`);
            this.halted = true;
            return;
        }
        
        // Get IDT entry
        const idtEntryAddr = this.idtBase + (exceptionNum * 8);
        const low = this.mem.read32(idtEntryAddr);
        const high = this.mem.read32(idtEntryAddr + 4);
        
        const offset = (high << 16) | (low & 0xFFFF);
        const selector = (low >> 16) & 0xFFFF;
        const typeAttr = high >> 16;
        
        // Check if handler is present
        if (!(typeAttr & 0x80)) {
            console.error(`Exception ${exceptionNum} handler not present!`);
            this.halted = true;
            return;
        }
        
        // Push error code (if any) and jump to handler
        // For now, simplified: just set EIP to handler
        this.regs.eip = offset;
        
        // In real implementation, we'd:
        // 1. Switch to handler's code segment
        // 2. Push EFLAGS, CS, EIP
        // 3. If error code, push it
        // 4. Set CS and EIP to handler
    }
    
    // Execute one instruction
    // Returns: cycles consumed (0 on error/halt)
    step() {
        if (this.halted) {
            return 0;
        }
        
        let cycles = 0;
        
        // Check breakpoints
        if (this.breakpoints.has(this.regs.eip)) {
            console.log(`Breakpoint hit at 0x${this.regs.eip.toString(16)}`);
            return 0;
        }
        
        // Decode and execute
        try {
            const opcode = this.readMem(this.regs.eip, 1);
            const startEip = this.regs.eip;
            
            if (this.debug) {
                console.log(`EIP=0x${this.regs.eip.toString(16)}, Opcode=0x${opcode.toString(16)}`);
            }
            
            // Handle prefixes
            this.prefixes = {
                segOverride: null,
                operandSize: false,
                addressSize: false,
                lock: false,
                rep: 0
            };
            
            let prefix = opcode;
            while (prefix === 0x66 || prefix === 0x67 || prefix === 0xF0 || 
                   prefix === 0xF2 || prefix === 0xF3 || 
                   (prefix >= 0x26 && prefix <= 0x3E && (prefix & 0x7) === 6)) {
                this.handlePrefix(prefix);
                this.regs.eip++;
                prefix = this.readMem(this.regs.eip, 1);
            }
            
            // Execute instruction
            cycles = this.executeInstruction(prefix);
            
            if (cycles === 0) {
                console.error(`Unhandled opcode: 0x${opcode.toString(16)} at EIP=0x${startEip.toString(16)}`);
                this.halted = true;
                return 0;
            }
            
            // Add cycles to T-state counter
            this.tstates += cycles;
            
            return cycles;
        } catch (e) {
            console.error(`CPU error at EIP=0x${this.regs.eip.toString(16)}:`, e);
            this.halted = true;
            return 0;
        }
    }
    
    // Handle instruction prefixes
    handlePrefix(prefix) {
        switch (prefix) {
            case 0x66: this.prefixes.operandSize = true; break;
            case 0x67: this.prefixes.addressSize = true; break;
            case 0xF0: this.prefixes.lock = true; break;
            case 0xF2: this.prefixes.rep = 2; break;  // REPNZ
            case 0xF3: this.prefixes.rep = 1; break;  // REPZ
            case 0x26: this.prefixes.segOverride = 'es'; break;
            case 0x2E: this.prefixes.segOverride = 'cs'; break;
            case 0x36: this.prefixes.segOverride = 'ss'; break;
            case 0x3E: this.prefixes.segOverride = 'ds'; break;
            case 0x64: this.prefixes.segOverride = 'fs'; break;
            case 0x65: this.prefixes.segOverride = 'gs'; break;
        }
    }
    
    // Execute instruction based on opcode
    // Returns: cycles consumed (0 on error)
    executeInstruction(opcode) {
        // Handle multi-byte opcodes (0x0F prefix)
        if (opcode === 0x0F) {
            this.regs.eip++;
            const opcode2 = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
            return this.executeExtendedInstruction(opcode2);
        }
        
        // Single-byte opcodes
        switch (opcode) {
            // NOP (3 cycles)
            case 0x90:
                this.regs.eip++;
                return 3;
                
            // MOV r32, imm32 (0xB8 + rd) (2 cycles)
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
                const regIndex = opcode - 0xB8;
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.setReg32(['eax','ecx','edx','ebx','esi','edi','esp','ebp'][regIndex], imm32);
                return 2;
            }
                
            // MOV r/m32, r32 (0x89) and MOV r32, r/m32 (0x8B)
            case 0x89: case 0x8B: {
                return this.handleMovRegMem(opcode);
            }
                
            // PUSH r32 (0x50 + rd) (2 cycles)
            case 0x50: case 0x51: case 0x52: case 0x53:
            case 0x54: case 0x55: case 0x56: case 0x57: {
                const regIndex = opcode - 0x50;
                const regValue = this.getReg32(['eax','ecx','edx','ebx','esp','ebp','esi','edi'][regIndex]);
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, regValue, 4);
                this.regs.eip++;
                return 2;
            }
                
            // POP r32 (0x58 + rd) (4 cycles)
            case 0x58: case 0x59: case 0x5A: case 0x5B:
            case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
                const regIndex = opcode - 0x58;
                const value = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.setReg32(['eax','ecx','edx','ebx','esp','ebp','esi','edi'][regIndex], value);
                this.regs.eip++;
                return 4;
            }
                
            // PUSH imm8 (0x6A) and PUSH imm32 (0x68)
            case 0x6A: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const imm32 = (imm8 & 0x80) ? (imm8 | 0xFFFFFF00) : imm8;
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, imm32, 4);
                return 2;
            }
            case 0x68: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, imm32, 4);
                return 2;
            }
                
            // RET (0xC3) and RET imm16 (0xC2) (4 cycles)
            case 0xC3: {
                const retAddr = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.regs.eip = retAddr;
                return 4;
            }
            case 0xC2: {
                const retAddr = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.regs.eip = retAddr;
                const imm16 = this.readMem(this.regs.eip, 2);
                this.regs.eip += 2;
                this.regs.esp += imm16;
                return 4;
            }
                
            // JMP rel32 (0xE9) and JMP rel8 (0xEB)
            case 0xE9: {
                this.regs.eip++;
                const rel32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.eip += (rel32 & 0x80000000) ? (rel32 | 0xFFFFFFFF80000000) : rel32;
                return 7;  // JMP rel32 = 7 cycles
            }
            case 0xEB: {
                this.regs.eip++;
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                this.regs.eip += (rel8 & 0x80) ? (rel8 - 256) : rel8;
                return 3;  // JMP rel8 = 3 cycles
            }
                
            // Jcc (0x74 = JE, 0x75 = JNE, etc.)
            case 0x74: return this.handleJcc(0x74);  // JE/JZ
            case 0x75: return this.handleJcc(0x75);  // JNE/JNZ
            case 0x7C: return this.handleJcc(0x7C);  // JL/JNGE
            case 0x7D: return this.handleJcc(0x7D);  // JGE/JNL
            case 0x7E: return this.handleJcc(0x7E);  // JLE/JNG
            case 0x7F: return this.handleJcc(0x7F);  // JG/JNLE
                
            // CMP AL, imm8 (0x3C), CMP EAX, imm32 (0x3D)
            case 0x3C: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const result = (this.regs.eax & 0xFF) - imm8;
                this.updateArithmeticFlags(result, this.regs.eax & 0xFF, imm8, (this.regs.eax & 0xFF) < imm8, true);
                return 2;
            }
            case 0x3D: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const result = this.regs.eax - imm32;
                this.updateArithmeticFlags(result, this.regs.eax, imm32, this.regs.eax < imm32, false);
                return 2;
            }
                
            // TEST AL, imm8 (0xA8), TEST EAX, imm32 (0xA9)
            case 0xA8: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const result = (this.regs.eax & 0xFF) & imm8;
                this.updateArithmeticFlags(result, 0, 0, 0, true);
                return 2;
            }
            case 0xA9: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const result = this.regs.eax & imm32;
                this.updateArithmeticFlags(result, 0, 0, 0, false);
                return 2;
            }
                
            // XOR r/m32, r32 (0x31) and XOR r32, r/m32 (0x33)
            case 0x31: case 0x33: {
                return this.handleXorRegMem(opcode);
            }
                
            // ADD r/m32, r32 (0x01) and ADD r32, r/m32 (0x03)
            case 0x01: case 0x03: {
                return this.handleAddRegMem(opcode);
            }
                
            // SUB r/m32, r32 (0x29) and SUB r32, r/m32 (0x2B)
            case 0x29: case 0x2B: {
                return this.handleSubRegMem(opcode);
            }
                
            // MOV r/m8, r8 (0x88) and MOV r8, r/m8 (0x8A)
            case 0x88: case 0x8A: {
                return this.handleMovRegMem8(opcode);
            }
                
            // INC/DEC r32 (0x40 + rd = INC, 0x48 + rd = DEC)
            case 0x40: case 0x41: case 0x42: case 0x43:
            case 0x44: case 0x45: case 0x46: case 0x47: {
                const regIndex = opcode - 0x40;
                const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][regIndex];
                const oldVal = this.getReg32(regName);
                const newVal = (oldVal + 1) >>> 0;
                this.setReg32(regName, newVal);
                this.updateArithmeticFlags(newVal, oldVal, 1, 0, false);
                this.setFlag('OF', oldVal === 0x7FFFFFFF);
                this.regs.eip++;
                return 2;  // INC = 2 cycles
            }
            case 0x48: case 0x49: case 0x4A: case 0x4B:
            case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
                const regIndex = opcode - 0x48;
                const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][regIndex];
                const oldVal = this.getReg32(regName);
                const newVal = (oldVal - 1) >>> 0;
                this.setReg32(regName, newVal);
                this.updateArithmeticFlags(newVal, oldVal, 1, 0, false);
                this.setFlag('OF', oldVal === 0x80000000);
                this.regs.eip++;
                return 2;  // DEC = 2 cycles
            }
                
            // CLI (0xFA), STI (0xFB) (3 cycles)
            case 0xFA:
                this.setFlag('IF', 0);
                this.regs.eip++;
                return 3;
            case 0xFB:
                this.setFlag('IF', 1);
                this.regs.eip++;
                return 3;
                
            // CLD (0xFC), STD (0xFD) (3 cycles)
            case 0xFC:
                this.setFlag('DF', 0);
                this.regs.eip++;
                return 3;
            case 0xFD:
                this.setFlag('DF', 1);
                this.regs.eip++;
                return 3;
                
            // IRET/IRETD (0xCF) (10 cycles)
            case 0xCF: {
                // Pop EIP, CS, EFLAGS
                this.regs.eip = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                const cs = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.segregs.cs = cs & 0xFFFF;
                this.eflags = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                return 10;
            }
                
            default:
                if (this.debug) {
                    console.log(`Unhandled opcode: 0x${opcode.toString(16)} at EIP=0x${this.regs.eip.toString(16)}`);
                }
                return 0;  // Error - unhandled opcode
        }
    }
    
    // Handle MOV r/m32, r32 (0x89) and MOV r32, r/m32 (0x8B)
    // Returns: cycles consumed
    handleMovRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;   // Register index
        const rm = modrm & 7;            // R/M index
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const regValue = this.getReg32(regName);
        
        // Get effective address
        let addr = 0;
        if (mod === 3) {
            // Register to register
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            if (opcode === 0x89) {
                // MOV r/m32, r32
                this.setReg32(rmName, regValue);
            } else {
                // MOV r32, r/m32
                this.setReg32(regName, this.getReg32(rmName));
            }
            return 2;  // Register-register MOV = 2 cycles
        } else {
            // Memory operand
            addr = this.calculateAddress(modrm);
            if (opcode === 0x89) {
                this.writeMem(addr, regValue, 4);
            } else {
                const memValue = this.readMem(addr, 4);
                this.setReg32(regName, memValue);
            }
            return 4;  // Register-memory MOV = 4 cycles
        }
    }
    
    // Handle XOR r/m32, r32 (0x31) and XOR r32, r/m32 (0x33)
    handleXorRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const regValue = this.getReg32(regName);
        
        if (mod === 3) {
            // Register to register
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            if (opcode === 0x31) {
                // XOR r/m32, r32
                const result = this.getReg32(rmName) ^ regValue;
                this.setReg32(rmName, result);
                this.updateArithmeticFlags(result, this.getReg32(rmName), regValue, 0, false);
            } else {
                // XOR r32, r/m32
                const result = regValue ^ this.getReg32(rmName);
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, this.getReg32(rmName), 0, false);
            }
            return 2;  // Register-register XOR = 2 cycles
        } else {
            // Memory operand (simplified - only register to memory or memory to register)
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x31) {
                // XOR r/m32, r32
                const memValue = this.readMem(addr, 4);
                const result = memValue ^ regValue;
                this.writeMem(addr, result, 4);
                this.updateArithmeticFlags(result, memValue, regValue, 0, false);
            } else {
                // XOR r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = regValue ^ memValue;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, 0, false);
            }
            return 4;  // Register-memory XOR = 4 cycles
        }
    }
    
    // Handle ADD r/m32, r32 (0x01) and ADD r32, r/m32 (0x03)
    handleAddRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const regValue = this.getReg32(regName);
        
        if (mod === 3) {
            // Register to register
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            if (opcode === 0x01) {
                // ADD r/m32, r32
                const result = (this.getReg32(rmName) + regValue) >>> 0;
                this.setReg32(rmName, result);
                this.updateArithmeticFlags(result, this.getReg32(rmName), regValue, result < this.getReg32(rmName), false);
            } else {
                // ADD r32, r/m32
                const result = (regValue + this.getReg32(rmName)) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, this.getReg32(rmName), result < regValue, false);
            }
            return 2;  // Register-register ADD = 2 cycles
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x01) {
                // ADD r/m32, r32
                const memValue = this.readMem(addr, 4);
                const result = (memValue + regValue) >>> 0;
                this.writeMem(addr, result, 4);
                this.updateArithmeticFlags(result, memValue, regValue, result < memValue, false);
            } else {
                // ADD r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = (regValue + memValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, result < regValue, false);
            }
            return 4;  // Register-memory ADD = 4 cycles
        }
    }
    
    // Handle SUB r/m32, r32 (0x29) and SUB r32, r/m32 (0x2B)
    handleSubRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const regValue = this.getReg32(regName);
        
        if (mod === 3) {
            // Register to register
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            if (opcode === 0x29) {
                // SUB r/m32, r32
                const result = (this.getReg32(rmName) - regValue) >>> 0;
                this.setReg32(rmName, result);
                this.updateArithmeticFlags(result, this.getReg32(rmName), regValue, this.getReg32(rmName) < regValue, false);
            } else {
                // SUB r32, r/m32
                const result = (regValue - this.getReg32(rmName)) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, this.getReg32(rmName), regValue < this.getReg32(rmName), false);
            }
            return 2;  // Register-register SUB = 2 cycles
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x29) {
                // SUB r/m32, r32
                const memValue = this.readMem(addr, 4);
                const result = (memValue - regValue) >>> 0;
                this.writeMem(addr, result, 4);
                this.updateArithmeticFlags(result, memValue, regValue, memValue < regValue, false);
            } else {
                // SUB r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = (regValue - memValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, regValue < memValue, false);
            }
            return 4;  // Register-memory SUB = 4 cycles
        }
    }
    
    // Handle MOV r/m8, r8 (0x88) and MOV r8, r/m8 (0x8A)
    handleMovRegMem8(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        // For 8-bit, reg field is: 0=AL, 1=CL, 2=DL, 3=BL, 4=AH, 5=CH, 6=DH, 7=BH
        const regNames8 = ['al','cl','dl','bl','ah','ch','dh','bh'];
        const regName = regNames8[reg];
        const regValue = this.getReg8(regName);
        
        if (mod === 3) {
            // Register to register
            const rmName = regNames8[rm];
            if (opcode === 0x88) {
                this.setReg8(rmName, regValue);
            } else {
                this.setReg8(regName, this.getReg8(rmName));
            }
            return 2;  // 8-bit register-register MOV = 2 cycles
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x88) {
                this.writeMem(addr, regValue, 1);
            } else {
                const memValue = this.readMem(addr, 1);
                this.setReg8(regName, memValue);
            }
            return 4;  // 8-bit register-memory MOV = 4 cycles
        }
    }
    
    // Calculate address from ModR/M byte
    calculateAddress(modrm) {
        const mod = (modrm >> 6) & 3;
        const rm = modrm & 7;
        
        if (mod === 0) {
            // [reg] or [disp32]
            if (rm === 5) {
                // [disp32]
                const disp32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                return disp32;
            } else {
                const baseReg = ['eax','ecx','edx','ebx','', 'ebp','esi','edi'][rm];
                if (baseReg) {
                    return this.getReg32(baseReg);
                } else {
                    // ESP - shouldn't happen in 32-bit mode without SIB
                    return this.regs.esp;
                }
            }
        } else if (mod === 1) {
            // [reg+disp8]
            const disp8 = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
            const baseReg = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            return (this.getReg32(baseReg) + (disp8 & 0x80 ? disp8 - 256 : disp8)) >>> 0;
        } else if (mod === 2) {
            // [reg+disp32]
            const disp32 = this.readMem(this.regs.eip, 4);
            this.regs.eip += 4;
            const baseReg = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            return (this.getReg32(baseReg) + disp32) >>> 0;
        }
        return 0;
    }
    
    // Handle conditional jumps (Jcc)
    // Returns: cycles consumed (7 if taken, 3 if not taken)
    handleJcc(opcode) {
        this.regs.eip++;
        const rel8 = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        let takeJump = false;
        switch (opcode) {
            case 0x74: takeJump = this.getFlag('ZF') === 1; break;  // JE/JZ
            case 0x75: takeJump = this.getFlag('ZF') === 0; break;  // JNE/JNZ
            case 0x7C: takeJump = (this.getFlag('SF') ^ this.getFlag('OF')) === 1; break;  // JL
            case 0x7D: takeJump = (this.getFlag('SF') ^ this.getFlag('OF')) === 0; break;  // JGE
            case 0x7E: takeJump = (this.getFlag('ZF') === 1) || (this.getFlag('SF') ^ this.getFlag('OF')) === 1; break;  // JLE
            case 0x7F: takeJump = (this.getFlag('ZF') === 0) && (this.getFlag('SF') ^ this.getFlag('OF')) === 0; break;  // JG
        }
        
        if (takeJump) {
            this.regs.eip += (rel8 & 0x80) ? (rel8 - 256) : rel8;
            return 7;  // Jcc taken = 7 cycles
        }
        return 3;  // Jcc not taken = 3 cycles
    }
    
    // Execute extended instructions (0x0F prefix)
    // Returns: cycles consumed (0 on error)
    executeExtendedInstruction(opcode2) {
        switch (opcode2) {
            // LGDT (0x01 /2) and LIDT (0x01 /3)
            case 0x01: {
                this.regs.eip--;  // Go back to re-read ModR/M
                return this.handleLgdtLidt();
            }
                
            // MOV to CRx (0x22) and MOV from CRx (0x20)
            case 0x20: case 0x22: {
                return this.handleMovCR(opcode2);
            }
                
            default:
                if (this.debug) {
                    console.log(`Unhandled extended opcode: 0x0F 0x${opcode2.toString(16)}`);
                }
                return 0;  // Error - unhandled
        }
    }
    
    // Handle LGDT and LIDT
    // Returns: cycles consumed
    handleLgdtLidt() {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;  // Should be 2 for LGDT, 3 for LIDT
        const rm = modrm & 7;
        
        // Get address from ModR/M
        const addr = this.calculateAddress(modrm);
        const limit = this.readMem(addr, 2);
        const base = this.readMem(addr + 2, 4);
        
        if (reg === 2) {
            // LGDT
            this.gdtBase = base;
            this.gdtLimit = limit;
            if (this.debug) {
                console.log(`LGDT: base=0x${base.toString(16)}, limit=0x${limit.toString(16)}`);
            }
        } else if (reg === 3) {
            // LIDT
            this.idtBase = base;
            this.idtLimit = limit;
            if (this.debug) {
                console.log(`LIDT: base=0x${base.toString(16)}, limit=0x${limit.toString(16)}`);
            }
        }
        
        return 3;  // LGDT/LIDT = 3 cycles
    }
    
    // Handle MOV to/from CRx
    // Returns: cycles consumed
    handleMovCR(opcode2) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const reg = (modrm >> 3) & 7;  // CR register number
        const rm = modrm & 7;          // General register
        const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
        
        if (opcode2 === 0x22) {
            // MOV r32, CRx (move to CR)
            const value = this.getReg32(rmName);
            if (reg === 0) {
                this.cregs.cr0 = value;
                if (this.debug) {
                    console.log(`MOV to CR0: 0x${value.toString(16)}`);
                }
            } else if (reg === 3) {
                this.cregs.cr3 = value;
                if (this.debug) {
                    console.log(`MOV to CR3: 0x${value.toString(16)}`);
                }
            }
            return 4;  // MOV to CR = 4 cycles
        } else if (opcode2 === 0x20) {
            // MOV CRx, r32 (move from CR)
            let value = 0;
            if (reg === 0) value = this.cregs.cr0;
            else if (reg === 3) value = this.cregs.cr3;
            this.setReg32(rmName, value);
            return 4;  // MOV from CR = 4 cycles
        }
        return 0;
    }
    
    // Run CPU for N cycles (instructions)
    run(cycles) {
        for (let i = 0; i < cycles; i++) {
            if (this.step() === 0) {
                break;
            }
        }
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = X86CPU;
}
if (typeof window !== 'undefined') {
    window.X86CPU = X86CPU;
}
// Also assign to 'this' (global in VM context)
if (typeof this !== 'undefined') {
    this.X86CPU = X86CPU;
}