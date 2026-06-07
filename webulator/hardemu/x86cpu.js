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
        
        // Debug registers (DR0-DR7)
        this.dregs = { dr0: 0, dr1: 0, dr2: 0, dr3: 0, dr6: 0, dr7: 0 };
        
        // EFLAGS register
        this.eflags = 0x00000002;  // Bit 1 always set
        
        // Memory subsystem (provided by memory.js)
        this.mem = memory;
        
        // PIC for interrupt handling
        this.pic = pic;
        
        // Machine (set by machine_x86.js for port I/O routing)
        this.machine = null;
        
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
        
        // EIP of the faulting instruction (for exception frame)
        this.faultEip = 0;
        
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
        
        // Track paging debug - trace first walks + always trace faults
        if (this._pagingDebugCount === undefined) this._pagingDebugCount = 0;
        const doTrace = this._pagingDebugCount < 25;
        
        // Always dump full state for suspicious addresses
        if ((vaddr >>> 22) >= 0x3F0) {
            console.log(`[PG] translateAddress(0x${vaddr.toString(16)}) EIP=0x${this.regs.eip.toString(16)}`);
            console.log(`[PG]   EAX=0x${(this.regs.eax>>>0).toString(16)} ECX=0x${(this.regs.ecx>>>0).toString(16)} EDX=0x${(this.regs.edx>>>0).toString(16)} EBX=0x${(this.regs.ebx>>>0).toString(16)}`);
            console.log(`[PG]   ESP=0x${(this.regs.esp>>>0).toString(16)} EBP=0x${(this.regs.ebp>>>0).toString(16)} ESI=0x${(this.regs.esi>>>0).toString(16)} EDI=0x${(this.regs.edi>>>0).toString(16)}`);
        }
        
        if (doTrace) {
            console.log(`[PG] translateAddress: vaddr=0x${vaddr.toString(16)} EIP=0x${this.regs.eip.toString(16)}`);
        }
        
        const pdIndex = (vaddr >>> 22) & 0x3FF;
        const ptIndex = (vaddr >>> 12) & 0x3FF;
        const offset = vaddr & 0xFFF;
        
        const pdBase = this.cregs.cr3 & 0xFFFFF000;  // Page directory base
        const pdeAddr = pdBase + (pdIndex * 4);
        const pde = this.mem.read32(pdeAddr);
        
        if (doTrace) {
            console.log(`[PG]   PD[${pdIndex}] at 0x${pdeAddr.toString(16)} = 0x${pde.toString(16)}`);
        }
        
        this._pagingDebugCount++;
        
        if (!(pde & 1)) {
            // Page not present - page fault
            console.log(`[PG]   PDE not present! #PF vaddr=0x${vaddr.toString(16)} at EIP=0x${this.regs.eip.toString(16)}`);
            console.log(`[PG]   Regs: EAX=0x${(this.regs.eax >>> 0).toString(16)} ECX=0x${(this.regs.ecx >>> 0).toString(16)} EDX=0x${(this.regs.edx >>> 0).toString(16)} EBX=0x${(this.regs.ebx >>> 0).toString(16)}`);
            console.log(`[PG]   Regs: ESP=0x${(this.regs.esp >>> 0).toString(16)} EBP=0x${(this.regs.ebp >>> 0).toString(16)} ESI=0x${(this.regs.esi >>> 0).toString(16)} EDI=0x${(this.regs.edi >>> 0).toString(16)}`);
            this.cregs.cr2 = vaddr;
            this.triggerException(14, 0);  // #PF
            return 0;
        }
        
        const ptBase = pde & 0xFFFFF000;
        const pteAddr = ptBase + (ptIndex * 4);
        const pte = this.mem.read32(pteAddr);
        
        if (doTrace) {
            console.log(`[PG]   PT[${ptIndex}] at 0x${pteAddr.toString(16)} = 0x${pte.toString(16)}`);
        }
        
        if (!(pte & 1)) {
            // Page not present - page fault
            console.log(`[PG]   PTE not present! Triggering #PF for vaddr=0x${vaddr.toString(16)} at EIP=0x${this.regs.eip.toString(16)}`);
            this.cregs.cr2 = vaddr;
            this.triggerException(14, 0);  // #PF
            return 0;
        }
        
        const physBase = pte & 0xFFFFF000;
        const physAddr = physBase + offset;
        
        if (doTrace) {
            console.log(`[PG]   → phys=0x${physAddr.toString(16)} (PDE=0x${pde.toString(16)}, PTE=0x${pte.toString(16)})`);
        }
        
        return physAddr;
    }
    
    // Trigger an exception
    triggerException(exceptionNum, errorCode) {
        if (this.debug || exceptionNum === 14) {
            console.log(`[EXC] Exception ${exceptionNum} (#${exceptionNum === 14 ? 'PF' : exceptionNum}) at EIP=0x${this.regs.eip.toString(16)} CR2=0x${this.cregs.cr2.toString(16)}`);
        }
        
        // Check if IDT is set up
        if (this.idtLimit === 0) {
            console.error(`Exception ${exceptionNum} but IDT not set up! EIP=0x${this.regs.eip.toString(16)}, CR2=0x${this.cregs.cr2 ? this.cregs.cr2.toString(16) : 'undefined'}`);
            this.halted = true;
            return;
        }
        
        // Get IDT entry (8 bytes per entry)
        // Bytes 0-1: Offset[15:0]
        // Bytes 2-3: Selector
        // Byte 4: Zero (reserved)
        // Byte 5: Type/Attributes
        // Bytes 6-7: Offset[31:16]
        // NOTE: idtBase is a virtual address (translated via page tables when paging is on)
        const idtEntryAddr = this.idtBase + (exceptionNum * 8);
        let low = this.readMem(idtEntryAddr, 4);
        let high = this.readMem(idtEntryAddr + 4, 4);
        low = low >>> 0;
        high = high >>> 0;
        
        // Offset[15:0] from low[15:0], Offset[31:16] from high[31:16]
        const offset = (((high >> 16) << 16) | (low & 0xFFFF)) >>> 0;
        // Selector from low[31:16]
        const selector = (low >> 16) & 0xFFFF;
        // Type/Attributes from high[15:8] (byte 5)
        const typeAttr = (high >> 8) & 0xFF;
        
        // Check if handler is present (bit 7 of typeAttr)
        if (!(typeAttr & 0x80)) {
            console.error(`Exception ${exceptionNum} handler not present! typeAttr=0x${typeAttr.toString(16)}`);
            this.halted = true;
            return;
        }
        
        // Push exception frame onto stack and jump to handler
        // Frame format (from top of stack downward):
        //   [ESP]   = error code (if applicable)
        //   [ESP+4] = EFLAGS
        //   [ESP+8] = CS
        //   [ESP+12] = EIP (return address)
        
        // Exceptions that push error codes: #DF(8), #TS(10), #NP(11), #SS(12), #GP(13), #PF(14)
        const hasErrorCode = (exceptionNum === 8 || exceptionNum === 10 || 
                              exceptionNum === 11 || exceptionNum === 12 || 
                              exceptionNum === 13 || exceptionNum === 14);
        
        // Push EFLAGS
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.eflags, 4);
        
        // Push CS (use current CS from segment registers)
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.segregs.cs, 4);
        
        // Push return EIP (address of the faulting instruction)
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.faultEip, 4);
        
        // Push error code if applicable
        if (hasErrorCode) {
            this.regs.esp -= 4;
            this.writeMem(this.regs.esp, errorCode || 0, 4);
        }
        
        // Jump to handler
        this.segregs.cs = selector;
        this.regs.eip = offset;
    }
    
    // Check PIC for pending interrupts; take one if IF=1
    checkInterrupts() {
        if (this.pic && this.getFlag('IF')) {
            this.pic.checkInterrupts();
        }
    }

    // Execute one instruction
    // Returns: cycles consumed (0 on error/halt)
    step() {
        // When halted, check for pending IRQs to wake up
        if (this.halted) {
            if (this.pic) {
                const pending = this.pic.master.irr & ~this.pic.master.imr & ~this.pic.master.isr;
                if (pending) {
                    this.halted = false;
                    this.checkInterrupts();
                }
            }
            if (this.halted) return 0;
        }
        
        let cycles = 0;
        
        // Check breakpoints
        if (this.breakpoints.has(this.regs.eip)) {
            console.log(`Breakpoint hit at 0x${this.regs.eip.toString(16)}`);
            return 0;
        }
        
        // Check pending interrupts before executing each instruction
        this.checkInterrupts();
        
        // Decode and execute
        try {
            const opcode = this.readMem(this.regs.eip, 1);
            this.faultEip = this.regs.eip;
            
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
            
            // Check if instruction halted the CPU (e.g., HLT)
            // Don't treat this as an error
            if (cycles === 0 && !this.halted) {
                console.error(`Unhandled opcode: 0x${opcode.toString(16)} at EIP=0x${this.faultEip.toString(16)}`);
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
        // SPECIAL HANDLING FOR HLT (0xF4) - before switch to ensure it's caught
        if (opcode === 0xF4) {
            console.log('HLT: Halting CPU');
            this.regs.eip++;  // Advance past HLT so interrupt save returns to next instruction
            this.halted = true;
            return 0;
        }
        
        // DEBUG: Log HLT opcode specifically
        if (opcode === 0xF4) {
            console.log(`DEBUG: executeInstruction received HLT (0xF4) at EIP=0x${(this.regs.eip - 1).toString(16)}`);
        }
        
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
            // With 0x66 prefix: MOV r16, imm16
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
                const regIndex = opcode - 0xB8;
                const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][regIndex];
                this.regs.eip++;
                if (this.prefixes.operandSize) {
                    // 0x66 prefix: MOV r16, imm16
                    const imm16 = this.readMem(this.regs.eip, 2);
                    this.regs.eip += 2;
                    // Preserve upper 16 bits, set lower 16 bits
                    this.regs[regName] = (this.regs[regName] & 0xFFFF0000) | (imm16 & 0xFFFF);
                } else {
                    const imm32 = this.readMem(this.regs.eip, 4);
                    this.regs.eip += 4;
                    this.setReg32(regName, imm32);
                }
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
                
            // I/O string instructions (0x6C-0x6F)
            case 0x6C:  // INSB
            case 0x6D:  // INSW/INSD
            case 0x6E:  // OUTSB
            case 0x6F:  // OUTSW/OUTSD
                return this.handleStringIO(opcode);
                
            // RET (0xC3) and RET imm16 (0xC2) (4 cycles)
            case 0xC3: {
                const retAddr = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.regs.eip = retAddr;
                return 4;
            }
            case 0xC2: {
                this.regs.eip++;
                const imm16 = this.readMem(this.regs.eip, 2);
                this.regs.eip += 2;
                const retAddr = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.regs.eip = retAddr;
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
            case 0xEA: {
                // JMP far ptr16:32 (direct far jump)
                // opcode 0xEA, offset (32-bit), selector (16-bit)
                this.regs.eip++;
                const offset32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const seg16 = this.readMem(this.regs.eip, 2);
                this.regs.eip += 2;
                this.regs.eip = offset32 >>> 0;
                this.segregs.cs = seg16 & 0xFFFF;
                return 10;  // JMP far = 10 cycles
            }
            case 0xEB: {
                this.regs.eip++;
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                this.regs.eip += (rel8 & 0x80) ? (rel8 - 256) : rel8;
                return 3;  // JMP rel8 = 3 cycles
            }
                
            // Jcc (0x70-0x7F conditional jumps)
            case 0x70: return this.handleJcc(0x70);  // JO
            case 0x71: return this.handleJcc(0x71);  // JNO
            case 0x72: return this.handleJcc(0x72);  // JB/JC
            case 0x73: return this.handleJcc(0x73);  // JNB/JNC
            case 0x74: return this.handleJcc(0x74);  // JE/JZ
            case 0x75: return this.handleJcc(0x75);  // JNE/JNZ
            case 0x76: return this.handleJcc(0x76);  // JBE
            case 0x77: return this.handleJcc(0x77);  // JA
            case 0x78: return this.handleJcc(0x78);  // JS
            case 0x79: return this.handleJcc(0x79);  // JNS
            case 0x7A: return this.handleJcc(0x7A);  // JP/JPE
            case 0x7B: return this.handleJcc(0x7B);  // JNP/JPO
            case 0x7C: return this.handleJcc(0x7C);  // JL/JNGE
            case 0x7D: return this.handleJcc(0x7D);  // JGE/JNL
            case 0x7E: return this.handleJcc(0x7E);  // JLE/JNG
            case 0x7F: return this.handleJcc(0x7F);  // JG/JNLE
            
            // LOOP instructions (0xE0, 0xE1, 0xE2)
            case 0xE0: {  // LOOPNE/LOOPNZ (loop while ECX!=0 and ZF=0)
                this.regs.eip++;
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                this.regs.ecx = (this.regs.ecx - 1) >>> 0;
                if (this.regs.ecx !== 0 && this.getFlag('ZF') === 0) {
                    const offset = (rel8 & 0x80) ? (rel8 - 256) : rel8;
                    this.regs.eip = (this.regs.eip + offset) >>> 0;
                }
                return 5;  // LOOP = 5 cycles
            }
            case 0xE1: {  // LOOPE/LOOPZ (loop while ECX!=0 and ZF=1)
                this.regs.eip++;
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                this.regs.ecx = (this.regs.ecx - 1) >>> 0;
                if (this.regs.ecx !== 0 && this.getFlag('ZF') === 1) {
                    const offset = (rel8 & 0x80) ? (rel8 - 256) : rel8;
                    this.regs.eip = (this.regs.eip + offset) >>> 0;
                }
                return 5;
            }
            case 0xE2: {  // LOOP (loop while ECX!=0)
                this.regs.eip++;  // Skip opcode
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;  // Skip rel8 (will be adjusted if jump taken)
                this.regs.ecx = (this.regs.ecx - 1) >>> 0;
                if (this.regs.ecx !== 0) {
                    const offset = (rel8 & 0x80) ? (rel8 - 256) : rel8;
                    this.regs.eip = (this.regs.eip + offset) >>> 0;
                }
                return 5;
            }
            case 0xE3: {  // JCXZ/JECXZ (jump if ECX=0)
                this.regs.eip++;
                const rel8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                if (this.regs.ecx === 0) {
                    const offset = (rel8 & 0x80) ? (rel8 - 256) : rel8;
                    this.regs.eip = (this.regs.eip + offset) >>> 0;
                }
                return 5;
            }
            
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
                
            // TEST r/m8, r8 (0x84), TEST r/m32, r32 (0x85)
            // AND r/m with r, set flags, don't store result
            case 0x84: case 0x85: {
                this.regs.eip++;
                const modrm = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const mod = (modrm >> 6) & 3;
                const reg = (modrm >> 3) & 7;
                const rm = modrm & 7;
                const isByteOp = (opcode === 0x84);
                
                let operand1, operand2;
                if (isByteOp) {
                    // 8-bit
                    const regName8 = ['al','cl','dl','bl','ah','ch','dh','bh'][reg];
                    operand1 = this.getReg8(regName8);
                    if (mod === 3) {
                        const rmName8 = ['al','cl','dl','bl','ah','ch','dh','bh'][rm];
                        operand2 = this.getReg8(rmName8);
                    } else {
                        const addr = this.calculateAddress(modrm);
                        operand2 = this.readMem(addr, 1);
                    }
                } else {
                    // 32-bit
                    const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
                    operand1 = this.getReg32(regName);
                    if (mod === 3) {
                        const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                        operand2 = this.getReg32(rmName);
                    } else {
                        const addr = this.calculateAddress(modrm);
                        operand2 = this.readMem(addr, 4);
                    }
                }
                
                const result = (operand1 & operand2) >>> 0;
                // TEST sets ZF, SF, PF; clears CF, OF
                this.updateArithmeticFlags(result, 0, 0, 0, isByteOp);
                return 2;  // TEST = 2 cycles
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
            
            // ADD r/m8, r8 (0x00) and ADD r8, r/m8 (0x02)
            case 0x00: case 0x02: {
                return this.handleAddRegMem8(opcode);
            }
            
            // ADD r/m32, r32 (0x01) and ADD r32, r/m32 (0x03)
            case 0x01: case 0x03: {
                return this.handleAddRegMem(opcode);
            }
            
            // ADD AL, imm8 (0x04) - short form for AL
            case 0x04: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const oldAl = this.regs.eax & 0xFF;
                const result = (oldAl + imm8) & 0xFF;
                this.regs.eax = (this.regs.eax & 0xFFFFFF00) | result;
                this.setFlag('CF', (oldAl + imm8) > 0xFF);
                this.updateArithmeticFlags(result, oldAl, imm8, undefined, true);
                return 2;
            }

            // AND AL, imm8 (0x24) - short form for AL
            case 0x24: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const result = (this.regs.eax & 0xFF) & imm8;
                this.regs.eax = (this.regs.eax & 0xFFFFFF00) | result;
                this.updateArithmeticFlags(result, 0, 0, 0, true);
                return 2;
            }
            
            // ADD EAX, imm32 (0x05) - short form for EAX
            case 0x05: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const oldEax = this.regs.eax;
                this.regs.eax = (this.regs.eax + imm32) >>> 0;
                this.setFlag('CF', (oldEax >>> 0) > (0xFFFFFFFF - (imm32 >>> 0)));
                this.updateArithmeticFlags(this.regs.eax, oldEax, imm32, undefined, false);
                return 2;
            }
                
            // PUSH ES (0x06), PUSH SS (0x16), PUSH DS (0x1E), PUSH CS (0x0E)
            case 0x06: case 0x0E: case 0x16: case 0x1E: {
                const segMap = { 0x06: 'es', 0x0E: 'cs', 0x16: 'ss', 0x1E: 'ds' };
                const segName = segMap[opcode];
                const segValue = this.segregs[segName] & 0xFFFF;
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, segValue, 4);
                this.regs.eip++;
                return 2;
            }
                
            // POP ES (0x07), POP SS (0x17), POP DS (0x1F)
            case 0x07: case 0x17: case 0x1F: {
                const segMap = { 0x07: 'es', 0x17: 'ss', 0x1F: 'ds' };
                const segName = segMap[opcode];
                const value = this.readMem(this.regs.esp, 4) & 0xFFFF;
                this.regs.esp += 4;
                this.segregs[segName] = value;
                this.regs.eip++;
                return 4;
            }
            
            // OR AL, imm8 (0x0C) - short form for AL
            case 0x0C: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const result = ((this.regs.eax & 0xFF) | imm8) & 0xFF;
                this.regs.eax = (this.regs.eax & 0xFFFFFF00) | result;
                this.updateArithmeticFlags(result, 0, 0, 0, true);
                return 2;
            }
            
            // OR EAX, imm32 (0x0d) - short form for EAX
            case 0x0d: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.eax = (this.regs.eax | imm32) >>> 0;
                this.updateArithmeticFlags(this.regs.eax, 0, 0, 0, false);
                return 2;
            }
            
            // AND EAX, imm32 (0x25) - short form for EAX
            case 0x25: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.eax = (this.regs.eax & imm32) >>> 0;
                this.updateArithmeticFlags(this.regs.eax, 0, 0, 0, false);
                return 2;
            }
            
            // SUB EAX, imm32 (0x2d) - short form for EAX
            case 0x2d: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const oldEax = this.regs.eax;
                this.regs.eax = (this.regs.eax - imm32) >>> 0;
                this.setFlag('CF', (oldEax >>> 0) < (imm32 >>> 0));
                this.updateArithmeticFlags(this.regs.eax, oldEax, imm32, true, false);
                return 2;
            }
            
            // XOR EAX, imm32 (0x35) - short form for EAX
            case 0x35: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.eax = (this.regs.eax ^ imm32) >>> 0;
                this.updateArithmeticFlags(this.regs.eax, 0, 0, 0, false);
                return 2;
            }
            
            // CMP EAX, imm32 (0x3d) - short form for EAX (flags only, no write)
            case 0x3d: {
                this.regs.eip++;
                const imm32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                const result = (this.regs.eax - imm32) >>> 0;
                this.setFlag('CF', (this.regs.eax >>> 0) < (imm32 >>> 0));
                this.updateArithmeticFlags(result, this.regs.eax, imm32, true, false);
                return 2;
            }
            
            // ADC r/m32, r32 (0x11) and ADC r32, r/m32 (0x13)
            case 0x11: case 0x13: {
                return this.handleAdcRegMem(opcode);
            }
            
            // ADC r/m8, r8 (0x10) and ADC r8, r/m8 (0x12) - 8-bit
            case 0x10: case 0x12: {
                return this.handleAdcRegMem8(opcode);
            }
            
            // OR r/m8, r8 (0x08), OR r/m32, r32 (0x09)
            // OR r8, r/m8 (0x0A), OR r32, r/m32 (0x0B)
            case 0x08: case 0x09: case 0x0A: case 0x0B: {
                return this.handleOrRegMem(opcode);
            }
            
            // SUB r/m8, r8 (0x28) and SUB r8, r/m8 (0x2A)
            case 0x28: case 0x2A: {
                return this.handleSubRegMem8(opcode);
            }
            
            // SUB r/m32, r32 (0x29) and SUB r32, r/m32 (0x2B)
            case 0x29: case 0x2B: {
                return this.handleSubRegMem(opcode);
            }
            
            // CMP r/m8, r8 (0x38), CMP r/m32, r32 (0x39)
            // CMP r8, r/m8 (0x3A), CMP r32, r/m32 (0x3B)
            case 0x38: case 0x39: case 0x3A: case 0x3B: {
                return this.handleCmpRegMem(opcode);
            }
            
            // AND r/m8, r8 (0x20), AND r/m32, r32 (0x21)
            // AND r8, r/m8 (0x22), AND r32, r/m32 (0x23)
            case 0x20: case 0x21: case 0x22: case 0x23: {
                return this.handleAndRegMem(opcode);
            }
            
            // Arithmetic with immediate: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP
            // 0x80: r/m8, imm8    (8-bit operand, 8-bit immediate)
            // 0x81: r/m32, imm32  (32-bit operand, 32-bit immediate)
            // 0x83: r/m32, imm8   (32-bit operand, 8-bit sign-extended immediate)
            case 0x80: case 0x81: case 0x83: {
                return this.handleArithmeticImmediate(opcode);
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
                
            // CLI (0xFA), STI (0xFB), HLT (0xF4), CMC (0xF5)
            case 0xFA:
                this.setFlag('IF', 0);
                this.regs.eip++;
                return 3;
            case 0xFB:
                this.setFlag('IF', 1);
                this.regs.eip++;
                return 3;
            case 0xF5:
                this.setFlag('CF', this.getFlag('CF') ^ 1);
                this.regs.eip++;
                return 2;
            case 0xF4:
                if (this.debug) {
                    console.log('HLT: Halting CPU');
                }
                this.halted = true;
                return 0;
            
            // PUSHAD (0x60) and POPAD (0x61)
            case 0x60: {
                // Push all 32-bit general-purpose registers
                // x86 order: EAX, ECX, EDX, EBX, ESP (original), EBP, ESI, EDI
                // Stack layout (low to high): EDI, ESI, EBP, old_ESP, EBX, EDX, ECX, EAX
                const origEsp = this.regs.esp - 32;
                this.regs.esp = origEsp;
                this.writeMem(origEsp + 28, this.regs.eax, 4);
                this.writeMem(origEsp + 24, this.regs.ecx, 4);
                this.writeMem(origEsp + 20, this.regs.edx, 4);
                this.writeMem(origEsp + 16, this.regs.ebx, 4);
                this.writeMem(origEsp + 12, origEsp + 32, 4);
                this.writeMem(origEsp + 8, this.regs.ebp, 4);
                this.writeMem(origEsp + 4, this.regs.esi, 4);
                this.writeMem(origEsp + 0, this.regs.edi, 4);
                this.regs.eip++;
                return 5;
            }
            case 0x61: {
                // Pop all 32-bit general-purpose registers (reverse order)
                this.regs.edi = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.esi = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.ebp = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.esp += 4;  // Skip original ESP (was popped but discarded)
                this.regs.ebx = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.edx = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.ecx = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.eax = this.readMem(this.regs.esp, 4); this.regs.esp += 4;
                this.regs.eip++;
                return 5;  // POPAD = 5 cycles
            }
            
            // PUSHF (0x9C) and POPF (0x9D) (3 cycles)
            case 0x9C: {
                // Push EFLAGS onto stack
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, this.eflags, 4);
                this.regs.eip++;
                return 3;
            }
            case 0x9D: {
                // Pop EFLAGS from stack
                this.eflags = this.readMem(this.regs.esp, 4);
                this.regs.esp += 4;
                this.regs.eip++;
                return 3;
            }
            case 0x9E: {
                // SAHF - Store AH into FLAGS (low 8 bits of EFLAGS)
                const ah = (this.regs.eax >> 8) & 0xFF;
                const oldEflags = this.eflags;
                this.eflags = (this.eflags & 0xFFFFFF00) | (ah & 0xD5) | 0x02;
                this.regs.eip++;
                return 2;
            }
            case 0x9F: {
                // LAHF - Load FLAGS into AH (low 8 bits of EFLAGS)
                const flagsLow = this.eflags & 0xFF;
                this.regs.eax = (this.regs.eax & 0xFFFF00FF) | ((flagsLow & 0xD5) << 8);
                this.regs.eip++;
                return 2;
            }
            
            // INT 3 (0xCC) - Breakpoint interrupt (3 cycles)
            case 0xCC: {
                this.handleInt(3);
                return 3;
            }
            
            // INT imm8 (0xCD) - Software interrupt (5 cycles)
            case 0xCD: {
                this.regs.eip++;  // Skip opcode
                const intNum = this.readMem(this.regs.eip, 1);
                this.regs.eip++;  // Skip interrupt number
                this.handleInt(intNum);
                return 5;
            }
            
            // MOV moffs8, AL (0xA2) and MOV moffs32, EAX (0xA3) (2 cycles)
            // MOV EAX, moffs32 (0xA1) (2 cycles)
            case 0xA1: {
                // MOV EAX, moffs32
                this.regs.eip++;
                const addr = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.regs.eax = this.readMem(addr, 4);
                return 2;
            }
            case 0xA2: {
                // MOV moffs8, AL
                this.regs.eip++;
                const addr = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.writeMem(addr, this.regs.eax & 0xFF, 1);
                return 2;
            }
            case 0xA3: {
                // MOV moffs32, EAX
                this.regs.eip++;
                const addr = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                this.writeMem(addr, this.regs.eax, 4);
                return 2;
            }
            
            // CALL rel32 (0xE8) - Call near, relative (5 cycles)
            case 0xE8: {
                this.regs.eip++;
                const rel32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                // Push return address
                this.regs.esp -= 4;
                this.writeMem(this.regs.esp, this.regs.eip, 4);
                // Jump to target
                this.regs.eip += (rel32 & 0x80000000) ? (rel32 | 0xFFFFFFFF80000000) : rel32;
                return 5;
            }
            
            // Group 5 instructions (0xFF /0-6)
            // INC r/m32, DEC r/m32, CALL r/m32, JMP r/m32, PUSH r/m32
            case 0xFF: {
                this.regs.eip++;
                const modrm = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const mod = (modrm >> 6) & 3;
                const reg = (modrm >> 3) & 7;
                const rm = modrm & 7;

                // Helper to get operand value and address
                let operandValue, operandAddr;
                if (mod === 3) {
                    // Register operand
                    const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                    operandValue = this.getReg32(rmName);
                    operandAddr = null;  // Not a memory address
                } else {
                    // Memory operand
                    operandAddr = this.calculateAddress(modrm);
                    operandValue = this.readMem(operandAddr, 4);
                }

                switch (reg) {
                    case 0: {  // INC r/m32
                        const oldVal = operandValue;
                        const newVal = (oldVal + 1) >>> 0;
                        if (mod === 3) {
                            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                            this.setReg32(rmName, newVal);
                        } else {
                            this.writeMem(operandAddr, newVal, 4);
                        }
                        // INC doesn't change CF - pass undefined for carry
                        this.updateArithmeticFlags(newVal, oldVal, 1, undefined, false);
                        // OF: set if signed overflow (0x7FFFFFFF -> 0x80000000)
                        this.setFlag('OF', oldVal === 0x7FFFFFFF);
                        return 2;  // INC r/m32 = 2 cycles
                    }

                    case 1: {  // DEC r/m32
                        const oldVal = operandValue;
                        const newVal = (oldVal - 1) >>> 0;
                        if (mod === 3) {
                            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                            this.setReg32(rmName, newVal);
                        } else {
                            this.writeMem(operandAddr, newVal, 4);
                        }
                        // DEC doesn't change CF - pass undefined for carry
                        this.updateArithmeticFlags(newVal, oldVal, 1, undefined, false);
                        // OF: set if signed overflow (0x80000000 -> 0x7FFFFFFF)
                        this.setFlag('OF', oldVal === 0x80000000);
                        return 2;  // DEC r/m32 = 2 cycles
                    }

                    case 2: {  // CALL r/m32 (near, absolute indirect)
                        let target;
                        if (mod === 3) {
                            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                            target = this.getReg32(rmName);
                        } else {
                            target = this.readMem(operandAddr, 4);
                        }
                        // Push return address
                        this.regs.esp -= 4;
                        this.writeMem(this.regs.esp, this.regs.eip, 4);
                        // Jump to target
                        this.regs.eip = target;
                        return 5;  // CALL r/m32 = 5 cycles
                    }

                    case 4: {  // JMP r/m32 (near, absolute indirect)
                        let target;
                        if (mod === 3) {
                            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                            target = this.getReg32(rmName);
                        } else {
                            target = this.readMem(operandAddr, 4);
                        }
                        this.regs.eip = target;
                        return 5;  // JMP r/m32 = 5 cycles
                    }

                    case 6: {  // PUSH r/m32
                        let value;
                        if (mod === 3) {
                            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                            value = this.getReg32(rmName);
                        } else {
                            value = this.readMem(operandAddr, 4);
                        }
                        this.regs.esp -= 4;
                        this.writeMem(this.regs.esp, value, 4);
                        return 2;  // PUSH r/m32 = 2 cycles
                    }

                    default:
                        return 0;  // Error - invalid reg field for 0xFF
                }
            }
            
            // LEA r32, m (0x8D) - Load effective address (2 cycles)
            case 0x8D: {
                this.regs.eip++;
                const modrm = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                
                const mod = (modrm >> 6) & 3;
                const reg = (modrm >> 3) & 7;   // Destination register
                const rm = modrm & 7;            // Source operand
                
                const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
                
                // Calculate address but don't dereference - that's the point of LEA
                let addr = 0;
                if (mod === 0) {
                    if (rm === 5) {
                        // [disp32]
                        addr = this.readMem(this.regs.eip, 4);
                        this.regs.eip += 4;
                    } else {
                        const baseReg = ['eax','ecx','edx','ebx','','ebp','esi','edi'][rm];
                        if (baseReg) {
                            addr = this.getReg32(baseReg);
                        }
                    }
                } else if (mod === 1) {
                    const disp8 = this.readMem(this.regs.eip, 1);
                    this.regs.eip++;
                    const baseReg = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                    addr = (this.getReg32(baseReg) + (disp8 & 0x80 ? disp8 - 256 : disp8)) >>> 0;
                } else if (mod === 2) {
                    const disp32 = this.readMem(this.regs.eip, 4);
                    this.regs.eip += 4;
                    const baseReg = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                    addr = (this.getReg32(baseReg) + disp32) >>> 0;
                }
                
                this.setReg32(regName, addr);
                return 2;
            }
            
            // MOV r8, imm8 (0xB0-0xB7) and MOV r32, imm32 (0xB8-0xBF)
            case 0xB0: case 0xB1: case 0xB2: case 0xB3:
            case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
                this.regs.eip++;
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
                this.setReg8(reg8Names[opcode & 7], imm8);
                return 2;
            }
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
                const operandSize = this.prefixes.operandSize ? 2 : 4;
                this.regs.eip++;
                const imm = this.readMem(this.regs.eip, operandSize);
                this.regs.eip += operandSize;
                const rmNames = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'];
                this.setReg32(rmNames[opcode & 7], imm);
                return 2;
            }
            
            // Shift/Rotate instructions (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3)
            // 0xC0/C1: GRP2 r/m8/r/m32, imm8
            // 0xD0/D1: GRP2 r/m8/r/m32, 1
            // 0xD2/D3: GRP2 r/m8/r/m32, CL
            case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
                return this.handleShiftRotate(opcode);
            }
            
            // MOV r/m8, imm8 (0xC6) and MOV r/m32, imm32 (0xC7)
            case 0xC6: case 0xC7: {
                return this.handleMovImm(opcode);
            }
            
            // MUL r/m8 (0xF6 /4) and MUL r/m32 (0xF7 /4)
            // IMUL r/m8 (0xF6 /5) and IMUL r/m32 (0xF7 /5)
            // DIV r/m8 (0xF6 /6) and DIV r/m32 (0xF7 /6)
            // IDIV r/m8 (0xF6 /7) and IDIV r/m32 (0xF7 /7)
            case 0xF6: case 0xF7: {
                return this.handleMulDiv(opcode);
            }
            
            // String operations: LODS/LODSB/LODSW/LODSD (0xAC, 0xAD)
            //                   STOS/STOSB/STOSW/STOSD (0xAA, 0xAB)
            //                   MOVS/MOVSB/MOVSW/MOVSD (0xA4, 0xA5)
            //                   CMPS/CMPSB/CMPSW/CMPSD (0xA6, 0xA7)
            //                   SCAS/SCASB/SCASW/SCASD (0xAE, 0xAF)
            case 0xA4: case 0xA5: case 0xA6: case 0xA7:
            case 0xAA: case 0xAB: case 0xAC: case 0xAD:
            case 0xAE: case 0xAF: {
                return this.handleStringOp(opcode);
            }
            
            // MOV r/m16, segment (0x8C) and MOV segment, r/m16 (0x8E)
            case 0x8C: case 0x8E: {
                return this.handleMovSegReg(opcode);
            }
                
            // CLD (0xFC), STD (0xFD) (3 cycles)
            case 0xFC:
                this.setFlag('DF', 0);
                this.regs.eip++;
                return 3;
            case 0xFD:
                this.setFlag('DF', 1);
                this.regs.eip++;
                return 3;
                
            // IN AL, imm8 (0xE4), IN AX, imm8 (0xE5), IN EAX, imm8 (0xE5 with operand size)
            // IN AL, DX (0xEC), IN AX, DX (0xED), IN EAX, DX (0xED with operand size)
            // OUT imm8, AL (0xE6), OUT imm8, AX (0xE7), OUT imm8, EAX (0xE7 with operand size)
            // OUT DX, AL (0xEE), OUT DX, AX (0xEF), OUT DX, EAX (0xEF with operand size)
            case 0xE4: {
                // IN AL, imm8
                this.regs.eip++;
                const port = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                // Read from I/O port - in emulator, this would call an I/O handler
                const value = this.handleIn(port, 1);
                this.regs.eax = (this.regs.eax & 0xFFFFFF00) | (value & 0xFF);
                return 3;
            }
            case 0xE5: {
                // IN (E)AX, imm8 (16 or 32 bit based on operand size prefix)
                this.regs.eip++;
                const port = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const size = this.prefixes.operandSize ? 2 : 4;
                const value = this.handleIn(port, size);
                if (size === 2) {
                    this.regs.eax = (this.regs.eax & 0xFFFF0000) | (value & 0xFFFF);
                } else {
                    this.regs.eax = value;
                }
                return 3;
            }
            case 0xEC: {
                // IN AL, DX
                const port = this.regs.edx & 0xFFFF;
                const value = this.handleIn(port, 1);
                this.regs.eax = (this.regs.eax & 0xFFFFFF00) | (value & 0xFF);
                this.regs.eip++;
                return 3;
            }
            case 0xED: {
                // IN (E)AX, DX (16 or 32 bit)
                const port = this.regs.edx & 0xFFFF;
                const size = this.prefixes.operandSize ? 2 : 4;
                const value = this.handleIn(port, size);
                if (size === 2) {
                    this.regs.eax = (this.regs.eax & 0xFFFF0000) | (value & 0xFFFF);
                } else {
                    this.regs.eax = value;
                }
                this.regs.eip++;
                return 3;
            }
            case 0xE6: {
                // OUT imm8, AL
                this.regs.eip++;
                const port = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const value = this.regs.eax & 0xFF;
                this.handleOut(port, value, 1);
                return 3;
            }
            case 0xE7: {
                // OUT imm8, (E)AX (16 or 32 bit)
                this.regs.eip++;
                const port = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const size = this.prefixes.operandSize ? 2 : 4;
                const value = size === 2 ? (this.regs.eax & 0xFFFF) : this.regs.eax;
                this.handleOut(port, value, size);
                return 3;
            }
            case 0xEE: {
                // OUT DX, AL
                const port = this.regs.edx & 0xFFFF;
                const value = this.regs.eax & 0xFF;
                this.handleOut(port, value, 1);
                this.regs.eip++;
                return 3;
            }
            case 0xEF: {
                // OUT DX, (E)AX (16 or 32 bit)
                const port = this.regs.edx & 0xFFFF;
                const size = this.prefixes.operandSize ? 2 : 4;
                const value = size === 2 ? (this.regs.eax & 0xFFFF) : this.regs.eax;
                this.handleOut(port, value, size);
                this.regs.eip++;
                return 3;
            }
            
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
                this.triggerException(6, 0);  // #UD — Undefined opcode
                return 10;
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
                const rmValue = this.getReg32(rmName);
                const result = (rmValue + regValue) >>> 0;
                this.setReg32(rmName, result);
                this.updateArithmeticFlags(result, rmValue, regValue, (result >>> 0) < (rmValue >>> 0), false);
            } else {
                // ADD r32, r/m32
                const rmValue = this.getReg32(rmName);
                const result = (regValue + rmValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, rmValue, (result >>> 0) < (regValue >>> 0), false);
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
                this.updateArithmeticFlags(result, memValue, regValue, (result >>> 0) < (memValue >>> 0), false);
            } else {
                // ADD r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = (regValue + memValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, (result >>> 0) < (regValue >>> 0), false);
            }
            return 4;  // Register-memory ADD = 4 cycles
        }
    }
    
    // Handle ADC r/m32, r32 (0x11) and ADC r32, r/m32 (0x13)
    handleAdcRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const regValue = this.getReg32(regName);
        const oldCF = this.getFlag('CF') ? 1 : 0;
        
        if (mod === 3) {
            // Register to register
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            if (opcode === 0x11) {
                // ADC r/m32, r32
                const destValue = this.getReg32(rmName);
                const result = (destValue + regValue + oldCF) >>> 0;
                this.setReg32(rmName, result);
                this.setFlag('CF', (destValue >>> 0) + (regValue >>> 0) + oldCF > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, destValue, regValue + oldCF, undefined, false);
            } else {
                // ADC r32, r/m32
                const destValue = this.getReg32(rmName);
                const result = (regValue + destValue + oldCF) >>> 0;
                this.setReg32(regName, result);
                this.setFlag('CF', (regValue >>> 0) + (destValue >>> 0) + oldCF > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, regValue, destValue + oldCF, undefined, false);
            }
            return 2;
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x11) {
                // ADC r/m32, r32
                const memValue = this.readMem(addr, 4);
                const result = (memValue + regValue + oldCF) >>> 0;
                this.writeMem(addr, result, 4);
                this.setFlag('CF', (memValue >>> 0) + (regValue >>> 0) + oldCF > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, memValue, regValue + oldCF, undefined, false);
            } else {
                // ADC r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = (regValue + memValue + oldCF) >>> 0;
                this.setReg32(regName, result);
                this.setFlag('CF', (regValue >>> 0) + (memValue >>> 0) + oldCF > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, regValue, memValue + oldCF, undefined, false);
            }
            return 4;
        }
    }
    
    // Handle ADC r/m8, r8 (0x10) and ADC r8, r/m8 (0x12) - 8-bit
    handleAdcRegMem8(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const reg8Names = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh'];
        const regName = reg8Names[reg];
        const regValue = this.getReg8(regName);
        const oldCF = this.getFlag('CF') ? 1 : 0;
        
        if (mod === 3) {
            // Register to register
            const rmName = reg8Names[rm];
            if (opcode === 0x10) {
                // ADC r/m8, r8
                const destValue = this.getReg8(rmName);
                const result = (destValue + regValue + oldCF) & 0xFF;
                this.setReg8(rmName, result);
                this.setFlag('CF', (destValue + regValue + oldCF) > 0xFF);
                this.updateArithmeticFlags(result, destValue, regValue + oldCF, undefined, true);
            } else {
                // ADC r8, r/m8
                const destValue = this.getReg8(rmName);
                const result = (regValue + destValue + oldCF) & 0xFF;
                this.setReg8(regName, result);
                this.setFlag('CF', (regValue + destValue + oldCF) > 0xFF);
                this.updateArithmeticFlags(result, regValue, destValue + oldCF, undefined, true);
            }
            return 2;
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x10) {
                // ADC r/m8, r8
                const memValue = this.mem.read8(addr);
                const result = (memValue + regValue + oldCF) & 0xFF;
                this.mem.write8(addr, result);
                this.setFlag('CF', (memValue + regValue + oldCF) > 0xFF);
                this.updateArithmeticFlags(result, memValue, regValue + oldCF, undefined, true);
            } else {
                // ADC r8, r/m8
                const memValue = this.mem.read8(addr);
                const result = (regValue + memValue + oldCF) & 0xFF;
                this.setReg8(regName, result);
                this.setFlag('CF', (regValue + memValue + oldCF) > 0xFF);
                this.updateArithmeticFlags(result, regValue, memValue + oldCF, undefined, true);
            }
            return 4;
        }
    }
    
    // Handle AND r/m8, r8 (0x20), AND r/m32, r32 (0x21)
    // AND r8, r/m8 (0x22), AND r32, r/m32 (0x23)
    handleAndRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const isByteOp = (opcode & 0x01) === 0x00;  // 0x20, 0x22 are 8-bit
        
        if (isByteOp) {
            // 8-bit AND (0x20, 0x22)
            const reg8Names = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh'];
            const regName = reg8Names[reg];
            const regValue = this.getReg8(regName);
            
            if (mod === 3) {
                // Register to register
                const rmName = reg8Names[rm];
                if (opcode === 0x20) {
                    // AND r/m8, r8
                    const result = (this.getReg8(rmName) & regValue) & 0xFF;
                    this.setReg8(rmName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                } else {
                    // AND r8, r/m8
                    const result = (regValue & this.getReg8(rmName)) & 0xFF;
                    this.setReg8(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                }
                return 2;
            } else {
                // Memory operand
                const addr = this.calculateAddress(modrm);
                if (opcode === 0x20) {
                    // AND r/m8, r8
                    const memValue = this.mem.read8(addr);
                    const result = (memValue & regValue) & 0xFF;
                    this.mem.write8(addr, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                } else {
                    // AND r8, r/m8
                    const memValue = this.mem.read8(addr);
                    const result = (regValue & memValue) & 0xFF;
                    this.setReg8(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                }
                return 4;
            }
        } else {
            // 32-bit AND (0x21, 0x23)
            const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
            const regValue = this.getReg32(regName);
            
            if (mod === 3) {
                // Register to register
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                if (opcode === 0x21) {
                    // AND r/m32, r32
                    const result = (this.getReg32(rmName) & regValue) >>> 0;
                    this.setReg32(rmName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                } else {
                    // AND r32, r/m32
                    const result = (regValue & this.getReg32(rmName)) >>> 0;
                    this.setReg32(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                }
                return 2;
            } else {
                // Memory operand
                const addr = this.calculateAddress(modrm);
                if (opcode === 0x21) {
                    // AND r/m32, r32
                    const memValue = this.read32(addr);
                    const result = (memValue & regValue) >>> 0;
                    this.write32(addr, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                } else {
                    // AND r32, r/m32
                    const memValue = this.read32(addr);
                    const result = (regValue & memValue) >>> 0;
                    this.setReg32(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                }
                return 4;
            }
        }
    }
    
    // Handle arithmetic with immediate: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP
    // 0x80: r/m8, imm8    (8-bit operand, 8-bit immediate)
    // 0x81: r/m32, imm32  (32-bit operand, 32-bit immediate)
    // 0x83: r/m32, imm8   (32-bit operand, 8-bit sign-extended immediate)
    handleArithmeticImmediate(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 0x3;
        const reg = (modrm >> 3) & 0x7;  // Operation type
        const rm = modrm & 0x7;
        
        // Determine operand size and read destination value
        const isByteOp = (opcode === 0x80);
        
        let destValue, destAddr, destRegName;
        
        if (mod === 0x3) {
            // Register operand
            if (isByteOp) {
                const reg8Names = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh'];
                destRegName = reg8Names[rm];
                destValue = this.getReg8(destRegName);
                destAddr = null;
            } else {
                const reg32Names = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'];
                destRegName = reg32Names[rm];
                destValue = this.getReg32(destRegName);
                destAddr = null;
            }
        } else {
            // Memory operand
            destAddr = this.calculateAddress(modrm);
            destValue = isByteOp ? this.mem.read8(destAddr) : this.mem.read32(destAddr);
            destRegName = null;
        }
        
        // Read immediate value
        let immValue;
        if (opcode === 0x80) {
            immValue = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
        } else if (opcode === 0x81) {
            immValue = this.readMem(this.regs.eip, 4);
            this.regs.eip += 4;
        } else {  // 0x83
            immValue = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
            // Sign-extend 8-bit to 32-bit
            if (immValue & 0x80) immValue |= 0xFFFFFF00;
        }
        
        // Execute operation based on reg field
        let result;
        const signedDest = isByteOp ? (destValue << 24) >> 24 : (destValue << 0) >> 0;
        const signedImm = isByteOp ? (immValue << 24) >> 24 : (immValue << 0) >> 0;
        
        switch (reg) {
            case 0x0:  // ADD
                result = (destValue + immValue) >>> 0;
                this.setFlag('CF', (destValue >>> 0) + (immValue >>> 0) > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, destValue, immValue, undefined, isByteOp);
                break;
            case 0x1:  // OR
                result = destValue | immValue;
                this.updateArithmeticFlags(result, 0, 0, 0, isByteOp);
                break;
            case 0x2:  // ADC (add with carry)
                const oldCF = this.getFlag('CF') ? 1 : 0;
                result = (destValue + immValue + oldCF) >>> 0;
                this.setFlag('CF', (destValue >>> 0) + (immValue >>> 0) + oldCF > 0xFFFFFFFF);
                this.updateArithmeticFlags(result, destValue, immValue + oldCF, undefined, isByteOp);
                break;
            case 0x3:  // SBB (subtract with borrow)
                const oldCF2 = this.getFlag('CF') ? 1 : 0;
                result = (destValue - immValue - oldCF2) >>> 0;
                this.setFlag('CF', (destValue >>> 0) < (immValue >>> 0) + oldCF2);
                this.updateArithmeticFlags(result, destValue, immValue + oldCF2, true, isByteOp);
                break;
            case 0x4:  // AND
                result = destValue & immValue;
                this.updateArithmeticFlags(result, 0, 0, 0, isByteOp);
                break;
            case 0x5:  // SUB
                result = (destValue - immValue) >>> 0;
                this.setFlag('CF', (destValue >>> 0) < (immValue >>> 0));
                this.updateArithmeticFlags(result, destValue, immValue, true, isByteOp);
                break;
            case 0x6:  // XOR
                result = destValue ^ immValue;
                this.updateArithmeticFlags(result, 0, 0, 0, isByteOp);
                break;
            case 0x7:  // CMP (compare, only sets flags)
                result = (destValue - immValue) >>> 0;
                this.setFlag('CF', (destValue >>> 0) < (immValue >>> 0));
                this.updateArithmeticFlags(result, destValue, immValue, true, isByteOp);
                result = destValue;  // CMP doesn't write result
                break;
        }
        
        // Write result back (unless it was CMP)
        if (reg !== 0x7) {
            if (mod === 0x3) {
                // Register destination
                if (isByteOp) {
                    this.setReg8(destRegName, result & 0xFF);
                } else {
                    this.setReg32(destRegName, result);
                }
            } else {
                // Memory destination
                if (isByteOp) {
                    this.mem.write8(destAddr, result & 0xFF);
                } else {
                    this.mem.write32(destAddr, result);
                }
            }
        }
        
        return isByteOp ? 2 : 4;  // 8-bit = 2 cycles, 32-bit = 4 cycles
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
                const rmValue = this.getReg32(rmName);
                const result = (rmValue - regValue) >>> 0;
                this.setReg32(rmName, result);
                this.updateArithmeticFlags(result, rmValue, regValue, (rmValue >>> 0) < (regValue >>> 0), false);
            } else {
                // SUB r32, r/m32
                const rmValue = this.getReg32(rmName);
                const result = (regValue - rmValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, rmValue, (regValue >>> 0) < (rmValue >>> 0), false);
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
                this.updateArithmeticFlags(result, memValue, regValue, (memValue >>> 0) < (regValue >>> 0), false);
            } else {
                // SUB r32, r/m32
                const memValue = this.readMem(addr, 4);
                const result = (regValue - memValue) >>> 0;
                this.setReg32(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, (regValue >>> 0) < (memValue >>> 0), false);
            }
            return 4;  // Register-memory SUB = 4 cycles
        }
    }
    
    // Handle CMP r/m, r (like SUB but doesn't store result)
    // 0x38: CMP r/m8, r8; 0x39: CMP r/m32, r32
    // 0x3A: CMP r8, r/m8; 0x3B: CMP r32, r/m32
    handleCmpRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const isByteOp = (opcode === 0x38 || opcode === 0x3A);
        
        if (isByteOp) {
            const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
            const regName = reg8Names[reg];
            const regValue = this.getReg8(regName);
            
            if (mod === 3) {
                const rmName = reg8Names[rm];
                let result;
                if (opcode === 0x38) {  // CMP r/m8, r8
                    const rmValue = this.getReg8(rmName);
                    result = (rmValue - regValue) & 0xFF;
                    this.updateArithmeticFlags(result, rmValue, regValue, rmValue < regValue, true);
                } else {  // CMP r8, r/m8
                    const rmValue = this.getReg8(rmName);
                    result = (regValue - rmValue) & 0xFF;
                    this.updateArithmeticFlags(result, regValue, rmValue, regValue < rmValue, true);
                }
            } else {
                const addr = this.calculateAddress(modrm);
                const memValue = this.readMem(addr, 1);
                if (opcode === 0x38) {
                    const result = (memValue - regValue) & 0xFF;
                    this.updateArithmeticFlags(result, memValue, regValue, memValue < regValue, true);
                } else {
                    const result = (regValue - memValue) & 0xFF;
                    this.updateArithmeticFlags(result, regValue, memValue, regValue < memValue, true);
                }
            }
        } else {
            const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
            const regValue = this.getReg32(regName);
            
            if (mod === 3) {
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                if (opcode === 0x39) {  // CMP r/m32, r32
                    const result = (this.getReg32(rmName) - regValue) >>> 0;
                    this.updateArithmeticFlags(result, this.getReg32(rmName), regValue, this.getReg32(rmName) < regValue, false);
                } else {  // CMP r32, r/m32
                    const result = (regValue - this.getReg32(rmName)) >>> 0;
                    this.updateArithmeticFlags(result, regValue, this.getReg32(rmName), regValue < this.getReg32(rmName), false);
                }
            } else {
                const addr = this.calculateAddress(modrm);
                const memValue = this.readMem(addr, 4);
                if (opcode === 0x39) {
                    const result = (memValue - regValue) >>> 0;
                    this.updateArithmeticFlags(result, memValue, regValue, memValue < regValue, false);
                } else {
                    const result = (regValue - memValue) >>> 0;
                    this.updateArithmeticFlags(result, regValue, memValue, regValue < memValue, false);
                }
            }
        }
        return 2;
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
    
    // Handle ADD r/m8, r8 (0x00) and ADD r8, r/m8 (0x02)
    handleAddRegMem8(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
        const regName = reg8Names[reg];
        const regValue = this.getReg8(regName);
        
        if (mod === 3) {
            const rmName = reg8Names[rm];
            if (opcode === 0x00) {
                const origRm = this.getReg8(rmName);
                const result = (origRm + regValue) & 0xFF;
                this.setReg8(rmName, result);
                this.updateArithmeticFlags(result, origRm, regValue, (result >>> 0) < (origRm >>> 0), true);
            } else {
                const origRm = this.getReg8(rmName);
                const result = (regValue + origRm) & 0xFF;
                this.setReg8(regName, result);
                this.updateArithmeticFlags(result, regValue, origRm, (result >>> 0) < (regValue >>> 0), true);
            }
            return 2;
        } else {
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x00) {
                const memValue = this.readMem(addr, 1);
                const result = (memValue + regValue) & 0xFF;
                this.writeMem(addr, result, 1);
                this.updateArithmeticFlags(result, memValue, regValue, result < memValue, true);
            } else {
                const memValue = this.readMem(addr, 1);
                const result = (regValue + memValue) & 0xFF;
                this.setReg8(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, result < regValue, true);
            }
            return 4;
        }
    }
    
    // Handle OR r/m8, r8 (0x08), OR r/m32, r32 (0x09)
    // OR r8, r/m8 (0x0A), OR r32, r/m32 (0x0B)
    handleOrRegMem(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const isByteOp = (opcode === 0x08 || opcode === 0x0A);
        
        if (isByteOp) {
            const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
            const regName = reg8Names[reg];
            const regValue = this.getReg8(regName);
            
            if (mod === 3) {
                const rmName = reg8Names[rm];
                if (opcode === 0x08) {
                    const result = (this.getReg8(rmName) | regValue) & 0xFF;
                    this.setReg8(rmName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                } else {
                    const result = (regValue | this.getReg8(rmName)) & 0xFF;
                    this.setReg8(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                }
                return 2;
            } else {
                const addr = this.calculateAddress(modrm);
                if (opcode === 0x08) {
                    const memValue = this.readMem(addr, 1);
                    const result = (memValue | regValue) & 0xFF;
                    this.writeMem(addr, result, 1);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                } else {
                    const memValue = this.readMem(addr, 1);
                    const result = (regValue | memValue) & 0xFF;
                    this.setReg8(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, true);
                }
                return 4;
            }
        } else {
            const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
            const regValue = this.getReg32(regName);
            
            if (mod === 3) {
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                if (opcode === 0x09) {
                    const result = (this.getReg32(rmName) | regValue) >>> 0;
                    this.setReg32(rmName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                } else {
                    const result = (regValue | this.getReg32(rmName)) >>> 0;
                    this.setReg32(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                }
                return 2;
            } else {
                const addr = this.calculateAddress(modrm);
                if (opcode === 0x09) {
                    const memValue = this.readMem(addr, 4);
                    const result = (memValue | regValue) >>> 0;
                    this.writeMem(addr, result, 4);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                } else {
                    const memValue = this.readMem(addr, 4);
                    const result = (regValue | memValue) >>> 0;
                    this.setReg32(regName, result);
                    this.updateArithmeticFlags(result, 0, 0, 0, false);
                }
                return 4;
            }
        }
    }
    
    // Handle SUB r/m8, r8 (0x28) and SUB r8, r/m8 (0x2A)
    handleSubRegMem8(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
        const regName = reg8Names[reg];
        const regValue = this.getReg8(regName);
        
        if (mod === 3) {
            const rmName = reg8Names[rm];
            if (opcode === 0x28) {
                const rmValue = this.getReg8(rmName);
                const result = (rmValue - regValue) & 0xFF;
                this.setReg8(rmName, result);
                this.updateArithmeticFlags(result, rmValue, regValue, rmValue < regValue, true);
            } else {
                const rmValue = this.getReg8(rmName);
                const result = (regValue - rmValue) & 0xFF;
                this.setReg8(regName, result);
                this.updateArithmeticFlags(result, regValue, rmValue, regValue < rmValue, true);
            }
            return 2;
        } else {
            const addr = this.calculateAddress(modrm);
            if (opcode === 0x28) {
                const memValue = this.readMem(addr, 1);
                const result = (memValue - regValue) & 0xFF;
                this.writeMem(addr, result, 1);
                this.updateArithmeticFlags(result, memValue, regValue, memValue < regValue, true);
            } else {
                const memValue = this.readMem(addr, 1);
                const result = (regValue - memValue) & 0xFF;
                this.setReg8(regName, result);
                this.updateArithmeticFlags(result, regValue, memValue, regValue < memValue, true);
            }
            return 4;
        }
    }
    
    // Handle MOV r/m8, imm8 (0xC6) and MOV r/m32, imm32 (0xC7)
    handleMovImm(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const rm = modrm & 7;
        const isByteOp = (opcode === 0xC6);
        
        if (isByteOp) {
            if (mod === 3) {
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                const reg8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
                this.setReg8(reg8Names[rm], imm8);
                return 2;
            } else {
                // Calculate address FIRST (consumes displacement bytes), then read imm8
                const addr = this.calculateAddress(modrm);
                const imm8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                this.writeMem(addr, imm8, 1);
                return 4;
            }
        } else {
            const operandSize = this.prefixes.operandSize ? 2 : 4;
            if (mod === 3) {
                const imm = this.readMem(this.regs.eip, operandSize);
                this.regs.eip += operandSize;
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                this.setReg32(rmName, imm);
                return 2;
            } else {
                // Calculate address FIRST (consumes displacement bytes), then read immediate
                const addr = this.calculateAddress(modrm);
                const imm = this.readMem(this.regs.eip, operandSize);
                this.regs.eip += operandSize;
                this.writeMem(addr, imm, operandSize);
                return 4;
            }
        }
    }
    
    // Calculate address from ModR/M byte
    calculateAddress(modrm) {
        const mod = (modrm >> 6) & 3;
        const rm = modrm & 7;
        
        // Helper to handle SIB byte
        const readSIB = () => {
            const sib = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
            const scale = 1 << ((sib >> 6) & 3);  // 1, 2, 4, or 8
            const index = (sib >> 3) & 7;
            const base = sib & 7;
            
            let effectiveAddr = 0;
            
            // Base register (unless mod=0 and base=5, then it's disp32-only)
            if (!(mod === 0 && base === 5)) {
                const baseRegName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][base];
                effectiveAddr = this.getReg32(baseRegName);
            }
            
            // Index register (index=4 means no index)
            if (index !== 4) {
                const indexRegName = ['eax','ecx','edx','ebx','_none','ebp','esi','edi'][index];
                effectiveAddr += this.getReg32(indexRegName) * scale;
            }
            
            return effectiveAddr >>> 0;
        };
        
        if (mod === 0) {
            // [reg] or [disp32]
            if (rm === 5) {
                // [disp32]
                const disp32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                return disp32 >>> 0;
            } else if (rm === 4) {
                // SIB follows, no displacement
                return readSIB();
            } else {
                const baseReg = ['eax','ecx','edx','ebx','', 'ebp','esi','edi'][rm];
                if (baseReg) {
                    return this.getReg32(baseReg) >>> 0;
                }
                return 0;
            }
        } else if (mod === 1) {
            // [reg+disp8]
            if (rm === 4) {
                // SIB follows, with disp8
                const effectiveAddr = readSIB();
                const disp8 = this.readMem(this.regs.eip, 1);
                this.regs.eip++;
                return (effectiveAddr + (disp8 & 0x80 ? (disp8 | 0xFFFFFF00) : disp8)) >>> 0;
            }
            const disp8 = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
            const baseReg = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            return (this.getReg32(baseReg) + (disp8 & 0x80 ? (disp8 | 0xFFFFFF00) : disp8)) >>> 0;
        } else if (mod === 2) {
            // [reg+disp32]
            if (rm === 4) {
                // SIB follows, with disp32
                const effectiveAddr = readSIB();
                const disp32 = this.readMem(this.regs.eip, 4);
                this.regs.eip += 4;
                return (effectiveAddr + disp32) >>> 0;
            }
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
            case 0x70: takeJump = this.getFlag('OF') === 1; break;  // JO
            case 0x71: takeJump = this.getFlag('OF') === 0; break;  // JNO
            case 0x72: takeJump = this.getFlag('CF') === 1; break;  // JB/JC
            case 0x73: takeJump = this.getFlag('CF') === 0; break;  // JNB/JNC
            case 0x74: takeJump = this.getFlag('ZF') === 1; break;  // JE/JZ
            case 0x75: takeJump = this.getFlag('ZF') === 0; break;  // JNE/JNZ
            case 0x76: takeJump = this.getFlag('CF') === 1 || this.getFlag('ZF') === 1; break;  // JBE
            case 0x77: takeJump = this.getFlag('CF') === 0 && this.getFlag('ZF') === 0; break;  // JA
            case 0x78: takeJump = this.getFlag('SF') === 1; break;  // JS
            case 0x79: takeJump = this.getFlag('SF') === 0; break;  // JNS
            case 0x7A: takeJump = this.getFlag('PF') === 1; break;  // JP/JPE
            case 0x7B: takeJump = this.getFlag('PF') === 0; break;  // JNP/JPO
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
    
    // Handle extended Jcc rel32 (0x0F 0x80-0x8F)
    // Same condition codes as short Jcc but with 32-bit displacement
    handleJccNear(opcode2) {
        const rel32 = this.readMem(this.regs.eip, 4);
        this.regs.eip += 4;
        
        // Map 0x80-0x8F to 0x70-0x7F condition codes
        const jccOpcode = opcode2 - 0x10;
        
        let takeJump = false;
        switch (jccOpcode) {
            case 0x70: takeJump = this.getFlag('OF') === 1; break;
            case 0x71: takeJump = this.getFlag('OF') === 0; break;
            case 0x72: takeJump = this.getFlag('CF') === 1; break;
            case 0x73: takeJump = this.getFlag('CF') === 0; break;
            case 0x74: takeJump = this.getFlag('ZF') === 1; break;
            case 0x75: takeJump = this.getFlag('ZF') === 0; break;
            case 0x76: takeJump = this.getFlag('CF') === 1 || this.getFlag('ZF') === 1; break;
            case 0x77: takeJump = this.getFlag('CF') === 0 && this.getFlag('ZF') === 0; break;
            case 0x78: takeJump = this.getFlag('SF') === 1; break;
            case 0x79: takeJump = this.getFlag('SF') === 0; break;
            case 0x7A: takeJump = this.getFlag('PF') === 1; break;
            case 0x7B: takeJump = this.getFlag('PF') === 0; break;
            case 0x7C: takeJump = (this.getFlag('SF') ^ this.getFlag('OF')) === 1; break;
            case 0x7D: takeJump = (this.getFlag('SF') ^ this.getFlag('OF')) === 0; break;
            case 0x7E: takeJump = (this.getFlag('ZF') === 1) || (this.getFlag('SF') ^ this.getFlag('OF')) === 1; break;
            case 0x7F: takeJump = (this.getFlag('ZF') === 0) && (this.getFlag('SF') ^ this.getFlag('OF')) === 0; break;
        }
        
        if (takeJump) {
            const signExtend = (rel32 & 0x80000000) ? (rel32 | 0xFFFFFFFF80000000) : rel32;
            this.regs.eip = (this.regs.eip + signExtend) >>> 0;
            return 7;  // Jcc near taken = 7 cycles
        }
        return 3;  // Jcc near not taken = 3 cycles
    }
    
    // MOVZX r32, r/m8 (0x0F 0xB6) and MOVZX r32, r/m16 (0x0F 0xB7)
    handleMovzx(opcode2) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const isByte = (opcode2 === 0xB6);
        
        let srcValue;
        if (mod === 3) {
            const rm8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
            const rm32Names = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'];
            srcValue = isByte ? this.getReg8(rm8Names[rm]) : (this.getReg32(rm32Names[rm]) & 0xFFFF);
        } else {
            const addr = this.calculateAddress(modrm);
            srcValue = this.readMem(addr, isByte ? 1 : 2);
        }
        
        this.setReg32(regName, srcValue >>> 0);
        return 2;
    }
    
    // MOVSX r32, r/m8 (0x0F 0xBE) and MOVSX r32, r/m16 (0x0F 0xBF)
    handleMovsx(opcode2) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const regName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const isByte = (opcode2 === 0xBE);
        
        let srcValue;
        if (mod === 3) {
            const rm8Names = ['al','cl','dl','bl','ah','ch','dh','bh'];
            const rm32Names = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'];
            srcValue = isByte ? this.getReg8(rm8Names[rm]) : (this.getReg32(rm32Names[rm]) & 0xFFFF);
        } else {
            const addr = this.calculateAddress(modrm);
            srcValue = this.readMem(addr, isByte ? 1 : 2);
        }
        
        // Sign extend
        if (isByte) {
            this.setReg32(regName, (srcValue & 0x80) ? (srcValue | 0xFFFFFF00) : srcValue);
        } else {
            this.setReg32(regName, (srcValue & 0x8000) ? (srcValue | 0xFFFF0000) : srcValue);
        }
        return 2;
    }
    
    // BT (0x0F 0xA3), BTS (0x0F 0xAB), BTR (0x0F 0xB3)
    handleBitTest(opcode2) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        const bitRegName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg];
        const bitPos = this.getReg32(bitRegName);
        
        if (mod === 3) {
            const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
            const destValue = this.getReg32(rmName);
            const maskedBit = bitPos & 0x1F;
            const bitValue = (destValue >> maskedBit) & 1;
            this.setFlag('CF', bitValue);
            
            if (opcode2 === 0xAB) {
                // BTS: set the bit
                this.setReg32(rmName, destValue | (1 << maskedBit));
            } else if (opcode2 === 0xB3) {
                // BTR: reset the bit
                this.setReg32(rmName, destValue & ~(1 << maskedBit));
            }
            return 2;
        } else {
            const addr = this.calculateAddress(modrm);
            // For memory operand, bit index can be > 31
            const byteOffset = (bitPos >> 3) & 0xFFF;
            const bitInByte = bitPos & 7;
            const actualAddr = (addr + byteOffset) >>> 0;
            const byteValue = this.readMem(actualAddr, 1);
            const bitValue = (byteValue >> bitInByte) & 1;
            this.setFlag('CF', bitValue);
            
            if (opcode2 === 0xAB) {
                // BTS: set the bit
                this.writeMem(actualAddr, byteValue | (1 << bitInByte), 1);
            } else if (opcode2 === 0xB3) {
                // BTR: reset the bit
                this.writeMem(actualAddr, byteValue & ~(1 << bitInByte), 1);
            }
            return 4;
        }
    }
    
    // Execute extended instructions (0x0F prefix)
    // Returns: cycles consumed (0 on error)
    executeExtendedInstruction(opcode2) {
        switch (opcode2) {
            // LGDT (0x01 /2) and LIDT (0x01 /3)
            // EIP already points to ModRM byte when we get here
            case 0x01: {
                return this.handleLgdtLidt();
            }
                
            // MOV to CRx (0x22) and MOV from CRx (0x20)
            case 0x20: case 0x22: {
                return this.handleMovCR(opcode2);
            }
                
            // MOV to DRx (0x23) and MOV from DRx (0x21)
            case 0x21: case 0x23: {
                return this.handleMovDR(opcode2);
            }
                
            // MOVZX r32, r/m8 (0xB6) and MOVZX r32, r/m16 (0xB7)
            case 0xB6: case 0xB7: {
                return this.handleMovzx(opcode2);
            }
                
            // MOVSX r32, r/m8 (0xBE) and MOVSX r32, r/m16 (0xBF)
            case 0xBE: case 0xBF: {
                return this.handleMovsx(opcode2);
            }
                
            // BT (0xA3), BTS (0xAB), BTR (0xB3)
            case 0xA3: case 0xAB: case 0xB3: {
                return this.handleBitTest(opcode2);
            }
                
            // Extended Jcc rel32 (0x80-0x8F)
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
                return this.handleJccNear(opcode2);
            }
                
            // Group 6/7: SLDT, STR, LLDT, LTR, VERR, VERW (0x00)
            case 0x00: {
                return this.handleGroup6_7();
            }
                
            // RDTSC (0x31) — Read Time-Stamp Counter
            case 0x31: {
                // Return a pseudo-TSC value (just incrementing counter)
                if (this._tsc === undefined) this._tsc = 0n;
                const tsc = this._tsc++;
                this.regs.eax = Number(tsc & 0xFFFFFFFFn);
                this.regs.edx = Number((tsc >> 32n) & 0xFFFFFFFFn);
                return 12;
            }
                
            // CPUID (0xA2) — CPU Identification
            case 0xA2: {
                // Return basic CPU identification info
                switch (this.regs.eax) {
                    case 0: {
                        // Maximum input value and vendor string
                        this.regs.eax = 1;
                        this.regs.ebx = 0x756E6547;  // "Genu"
                        this.regs.ecx = 0x6C65746E;  // "ntel"
                        this.regs.edx = 0x49656E69;  // "ineI"
                        return 10;
                    }
                    case 1: {
                        // Processor info and feature bits
                        this.regs.eax = 0x00000600;  // Stepping=0, Model=6, Family=6
                        this.regs.ebx = 0x00000000;
                        this.regs.ecx = 0x00000000;  // No SSE3, no monitor
                        this.regs.edx = 0x0603FBFF;  // FPU, VME, DE, PSE, TSC, MSR, PAE, MCE, CX8, APIC, SEP, MTRR, PGE, MCA, CMOV, PAT, PSE36, MMX, FXSR
                        return 10;
                    }
                    default: {
                        this.regs.eax = 0;
                        this.regs.ebx = 0;
                        this.regs.ecx = 0;
                        this.regs.edx = 0;
                        return 10;
                    }
                }
            }
                
            // INVD (0x08) — Invalidate cache, no-op in emulation
            case 0x08: {
                // Privileged instruction — invalidates internal cache
                // We don't model cache, so this is a no-op
                // Need to still read the ModRM byte (even though unused)
                this.regs.eip++;
                return 3;
            }
                
            default:
                this.triggerException(6, 0);  // #UD — Undefined opcode
                return 10;
        }
    }
    
    // Handle LGDT and LIDT
    // Returns: cycles consumed
    handleLgdtLidt() {
        // NOTE: EIP already points to ModRM byte (executeInstruction advanced it)
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
        // NOTE: EIP already points to ModRM byte (executeInstruction advanced it)
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
                if (this.debug || (value & 0x80000000)) {
                    console.log(`[PG] MOV to CR0: 0x${value.toString(16)} (PG=${(value & 0x80000000) ? 'ENABLED' : 'off'}) CR3=0x${this.cregs.cr3.toString(16)} EIP=0x${this.regs.eip.toString(16)}`);
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
        this.triggerException(6, 0);  // #UD — Invalid CR register
        return 10;
    }
    
    // Handle MOV to/from Debug Registers (0x0F 0x21/0x23)
    handleMovDR(opcode2) {
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const reg = (modrm >> 3) & 7;  // Debug register number
        const rm = modrm & 7;           // General-purpose register
        const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
        
        if (opcode2 === 0x23) {
            // MOV r32, DRx — move to debug register
            const value = this.getReg32(rmName);
            if (reg <= 3) {
                this.dregs['dr' + reg] = value;
            } else if (reg === 6) {
                this.dregs.dr6 = (value & 0xFFFF0FF0) | 0xF;
            } else if (reg === 7) {
                this.dregs.dr7 = value;
            }
            return 4;
        } else {
            // MOV DRx, r32 — move from debug register
            let value = 0;
            if (reg <= 3) {
                value = this.dregs['dr' + reg];
            } else if (reg === 6) {
                value = this.dregs.dr6;
            } else if (reg === 7) {
                value = this.dregs.dr7;
            }
            this.setReg32(rmName, value);
            return 4;
        }
    }
    
    // Handle Shift/Rotate instructions
    // opcodes: 0xC0, 0xC1 (imm8), 0xD0, 0xD1 (1), 0xD2, 0xD3 (CL)
    handleShiftRotate(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;  // Operation type
        const rm = modrm & 7;
        
        // Determine operand size
        const isByteOp = (opcode === 0xC0 || opcode === 0xD0 || opcode === 0xD2);
        const mask = isByteOp ? 0xFF : 0xFFFFFFFF;
        const bitWidth = isByteOp ? 8 : 32;
        
        // Get count (how many bits to shift/rotate)
        let count = 0;
        if (opcode === 0xC0 || opcode === 0xC1) {
            // imm8
            count = this.readMem(this.regs.eip, 1);
            this.regs.eip++;
        } else if (opcode === 0xD0 || opcode === 0xD1) {
            // 1
            count = 1;
        } else {
            // CL
            count = this.regs.ecx & 0xFF;
        }
        
        // Get operand value
        let value = 0;
        let result = 0;
        
        if (mod === 3) {
            // Register operand
            if (isByteOp) {
                const regNames8 = ['al','cl','dl','bl','ah','ch','dh','bh'];
                const rmName = regNames8[rm];
                value = this.getReg8(rmName);
            } else {
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                value = this.getReg32(rmName);
            }
        } else {
            // Memory operand
            const addr = this.calculateAddress(modrm);
            value = this.readMem(addr, isByteOp ? 1 : 4);
        }
        
        // Perform operation
        switch (reg) {
            case 0:  // ROL (Rotate Left)
                count %= bitWidth;
                result = value >>> 0;  // Convert to unsigned
                if (this.debug) {
                    console.log(`ROL: value=0x${(value >>> 0).toString(16)}, count=${count}`);
                }
                for (let i = 0; i < count; i++) {
                    // Extract MSB (bit 31 for 32-bit, bit 7 for 8-bit)
                    const msb = (result & (1 << (bitWidth - 1))) ? 1 : 0;
                    // Shift left by 1 (using arithmetic to avoid JS signed issues)
                    const shifted = ((result & (mask >>> 1)) * 2) & mask;
                    // Bring MSB to LSB
                    result = (shifted | msb) >>> 0;
                    if (this.debug) {
                        console.log(`  iter ${i}: msb=${msb}, shifted=0x${(shifted >>> 0).toString(16)}, result=0x${(result >>> 0).toString(16)}`);
                    }
                }
                // CF is set to the last bit rotated out (MSB of final result for ROL)
                this.setFlag('CF', (result & 1));
                if (count === 1) {
                    // OF is set if result sign bit != CF (only for 1-bit rotation)
                    this.setFlag('OF', ((result >> (bitWidth - 1)) & 1) ^ (result & 1));
                }
                if (this.debug) {
                    console.log(`ROL result: 0x${(result >>> 0).toString(16)}, CF=${this.getFlag('CF')}`);
                }
                break;
                
            case 1:  // ROR (Rotate Right)
                count %= bitWidth;
                result = value >>> 0;  // Convert to unsigned
                for (let i = 0; i < count; i++) {
                    // Extract LSB (bit 0)
                    const lsb = result & 1;
                    // Shift right by 1 (using >>> which is unsigned right shift)
                    const shifted = (result >>> 1) & mask;
                    // Bring LSB to MSB
                    const rotated = lsb ? (1 << (bitWidth - 1)) : 0;
                    result = (shifted | rotated) >>> 0;
                }
                this.setFlag('CF', (result >> (bitWidth - 1)) & 1);
                if (count === 1) {
                    this.setFlag('OF', ((result >> (bitWidth - 1)) ^ (result >> (bitWidth - 2))) & 1);
                }
                break;
                
            case 2:  // RCL (Rotate through Carry Left)
                count %= (bitWidth + 1);
                const cf = this.getFlag('CF');
                result = ((value << count) | (cf << (count - 1))) & mask;
                this.setFlag('CF', (value >> (bitWidth - count)) & 1);
                break;
                
            case 3:  // RCR (Rotate through Carry Right)
                count %= (bitWidth + 1);
                const cf2 = this.getFlag('CF');
                result = ((value >>> count) | (cf2 << (bitWidth - count))) & mask;
                this.setFlag('CF', (value >> (count - 1)) & 1);
                break;
                
            case 4:  // SHL/SAL (Shift Left)
                result = (value << count) & mask;
                this.updateArithmeticFlags(result, 0, 0, (value >> (bitWidth - count)) & 1, isByteOp);
                this.setFlag('OF', ((result >> (bitWidth - 1)) ^ (this.getFlag('CF') << (bitWidth - 1))) & 1);
                break;
                
            case 5:  // SHR (Shift Right)
                result = (value >>> count) & mask;
                this.updateArithmeticFlags(result, 0, 0, (value >> (count - 1)) & 1, isByteOp);
                this.setFlag('OF', (value >> (bitWidth - 1)) & 1);
                break;
                
            case 7:  // SAR (Arithmetic Shift Right)
                const signBit = value & (1 << (bitWidth - 1));
                result = (value >> count) & mask;
                if (signBit) {
                    // Sign extend
                    for (let i = 0; i < count; i++) {
                        result |= (1 << (bitWidth - 1 - i));
                    }
                }
                this.updateArithmeticFlags(result, 0, 0, (value >> (count - 1)) & 1, isByteOp);
                this.setFlag('OF', 0);
                break;
                
            default:
                if (this.debug) {
                    console.log(`Unhandled shift/rotate operation: ${reg}`);
                }
                return 0;
        }
        
        // Write back result
        if (mod === 3) {
            if (isByteOp) {
                const regNames8 = ['al','cl','dl','bl','ah','ch','dh','bh'];
                this.setReg8(regNames8[rm], result);
            } else {
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                this.setReg32(rmName, result);
            }
        } else {
            const addr = this.calculateAddress(modrm);
            this.writeMem(addr, result, isByteOp ? 1 : 4);
        }
        
        return 3;  // Shift/rotate = 3 cycles (typical)
    }
    
    // Handle MUL/DIV/IMUL/IDIV/NOT/NEG instructions
    handleMulDiv(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;  // Operation type
        const rm = modrm & 7;
        
        const isByteOp = (opcode === 0xF6);
        const isSigned = (reg === 5 || reg === 7);  // IMUL or IDIV
        
        // Get operand value (save address/reg name for write-back by NOT/NEG)
        let operand = 0;
        let operandAddr = null;
        let operandRegName = null;
        if (mod === 3) {
            if (isByteOp) {
                const regNames8 = ['al','cl','dl','bl','ah','ch','dh','bh'];
                operandRegName = regNames8[rm];
                operand = this.getReg8(operandRegName);
            } else {
                const rmNames = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'];
                operandRegName = rmNames[rm];
                operand = this.getReg32(operandRegName);
            }
        } else {
            operandAddr = this.calculateAddress(modrm);
            operand = this.readMem(operandAddr, isByteOp ? 1 : 4);
        }
        
        // NOT (reg=2) and NEG (reg=3) — work for both 8 and 32-bit
        if (reg === 2) {
            // NOT r/m — bitwise complement, no flags affected
            const mask = isByteOp ? 0xFF : 0xFFFFFFFF;
            const result = (~operand) & mask;
            if (mod === 3) {
                if (isByteOp) {
                    this.setReg8(operandRegName, result);
                } else {
                    this.setReg32(operandRegName, result);
                }
            } else {
                this.writeMem(operandAddr, result, isByteOp ? 1 : 4);
            }
            return 2;
        } else if (reg === 3) {
            // NEG r/m — two's complement negation, sets flags
            const mask = isByteOp ? 0xFF : 0xFFFFFFFF;
            const result = ((~operand) + 1) & mask;
            this.setFlag('CF', (operand & mask) !== 0);
            this.updateArithmeticFlags(result, 0, operand, undefined, isByteOp);
            if (mod === 3) {
                if (isByteOp) {
                    this.setReg8(operandRegName, result);
                } else {
                    this.setReg32(operandRegName, result);
                }
            } else {
                this.writeMem(operandAddr, result, isByteOp ? 1 : 4);
            }
            return 2;
        }

        if (isByteOp) {
            // 8-bit operations
            if (reg === 4 || reg === 5) {
                // MUL/IMUL r/m8: AX = AL * r/m8
                let result;
                if (isSigned) {
                    // IMUL - sign extend
                    const al = (this.regs.eax & 0x80) ? (this.regs.eax | 0xFFFFFF00) : (this.regs.eax & 0xFF);
                    const op = (operand & 0x80) ? (operand | 0xFFFFFF00) : operand;
                    result = al * op;
                } else {
                    // MUL
                    result = (this.regs.eax & 0xFF) * operand;
                }
                this.regs.eax = result & 0xFFFF;
                this.setFlag('CF', (result & 0xFF00) !== 0);
                this.setFlag('OF', (result & 0xFF00) !== 0);
                return 11;  // MUL r8 = 11 cycles
            } else if (reg === 6 || reg === 7) {
                // DIV/IDIV r/m8: AL = AX / r/m8, AH = AX % r/m8
                const dividend = this.regs.eax & 0xFFFF;
                if (operand === 0) {
                    this.triggerException(0, 0);  // #DE - Divide Error
                    return 0;
                }
                if (isSigned) {
                    // IDIV
                    const al = (dividend & 0x80) ? (dividend | 0xFFFFFF00) : dividend;
                    const op = (operand & 0x80) ? (operand | 0xFFFFFF00) : operand;
                    const quotient = Math.trunc(al / op);
                    const remainder = al % op;
                    this.regs.eax = (this.regs.eax & 0xFFFF0000) | ((quotient & 0xFF) | ((remainder & 0xFF) << 8));
                } else {
                    // DIV
                    const quotient = Math.floor(dividend / operand);
                    const remainder = dividend % operand;
                    if (quotient > 0xFF) {
                        this.triggerException(0, 0);  // #DE
                        return 0;
                    }
                    this.regs.eax = (this.regs.eax & 0xFFFF0000) | (quotient & 0xFF) | ((remainder & 0xFF) << 8);
                }
                return 14;  // DIV r8 = 14 cycles
            }
        } else {
            // 32-bit operations
            if (reg === 4 || reg === 5) {
                // MUL/IMUL r/m32: EDX:EAX = EAX * r/m32
                let result;
                if (isSigned) {
                    // IMUL
                    const eax = (this.regs.eax & 0x80000000) ? (this.regs.eax | 0xFFFFFFFF00000000) : this.regs.eax;
                    const op = (operand & 0x80000000) ? (operand | 0xFFFFFFFF00000000) : operand;
                    result = BigInt(eax) * BigInt(op);
                } else {
                    // MUL
                    result = BigInt(this.regs.eax) * BigInt(operand);
                }
                this.regs.eax = Number(result & 0xFFFFFFFFn);
                this.regs.edx = Number((result >> 32n) & 0xFFFFFFFFn);
                this.setFlag('CF', (result >> 32n) !== 0n);
                this.setFlag('OF', (result >> 32n) !== 0n);
                return 11;  // MUL r32 = 11 cycles
            } else if (reg === 6 || reg === 7) {
                // DIV/IDIV r/m32: EAX = EDX:EAX / r/m32, EDX = EDX:EAX % r/m32
                const dividend = (BigInt(this.regs.edx) << 32n) | BigInt(this.regs.eax);
                if (operand === 0) {
                    this.triggerException(0, 0);  // #DE
                    return 0;
                }
                if (isSigned) {
                    // IDIV
                    const quotient = Number(dividend / BigInt(operand));
                    const remainder = Number(dividend % BigInt(operand));
                    this.regs.eax = quotient;
                    this.regs.edx = remainder;
                } else {
                    // DIV
                    const quotient = Number(dividend / BigInt(operand));
                    const remainder = Number(dividend % BigInt(operand));
                    if (quotient > 0xFFFFFFFF) {
                        this.triggerException(0, 0);  // #DE
                        return 0;
                    }
                    this.regs.eax = quotient;
                    this.regs.edx = remainder;
                }
                return 14;  // DIV r32 = 14 cycles
            }
        }
        
        this.triggerException(6, 0);  // #UD — Invalid reg field for MUL/DIV
        return 10;
    }
    
    // Handle INT (software interrupt)
    handleInt(intNum) {
        // Wake CPU from HLT when interrupt arrives
        this.halted = false;
        
        if (this.debug) {
            console.log(`INT 0x${intNum.toString(16)} at EIP=0x${this.regs.eip.toString(16)}`);
        }
        
        // Push EFLAGS, CS, EIP (5 cycles for INT)
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.eflags, 4);
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.segregs.cs & 0xFFFF, 2);  // CS as 16-bit value (real mode)
        this.regs.esp -= 2;  // Real mode pushes 16-bit CS
        this.regs.esp += 2;  // Adjust back to 32-bit push for our implementation
        this.regs.esp -= 4;
        this.writeMem(this.regs.esp, this.regs.eip, 4);
        
        // Clear IF, TF, RF flags
        this.setFlag('IF', 0);
        this.setFlag('TF', 0);
        this.setFlag('RF', 0);
        
        // Get interrupt handler from IDT
        if (this.idtLimit === 0) {
            // Real mode - use IVT at 0x0000:0x0000
            const ivtAddr = intNum * 4;
            const offset = this.readMem(ivtAddr, 2);
            const segment = this.readMem(ivtAddr + 2, 2);
            this.segregs.cs = segment;
            this.regs.eip = offset;
        } else {
            // Protected mode - use IDT
            const idtEntryAddr = this.idtBase + (intNum * 8);
            const low = this.readMem(idtEntryAddr, 4);
            const high = this.readMem(idtEntryAddr + 4, 4);
            
            const offset = ((high & 0xFFFF0000) | (low & 0xFFFF)) >>> 0;
            const selector = (low >> 16) & 0xFFFF;
            
            this.segregs.cs = selector;
            this.regs.eip = offset;
        }
    }
    
    // Handle IN (port I/O read)
    handleIn(port, size) {
        // Delegate to machine if available (routes to proper device handlers)
        if (this.machine) {
            return this.machine.cpuPortRead(port);
        }
        
        if (this.debug) {
            console.log(`IN from port 0x${port.toString(16)}, size=${size}`);
        }
        
        return 0;
    }
    
    // Handle OUT (port I/O write)
    handleOut(port, value, size) {
        // Delegate to machine if available (routes to proper device handlers)
        if (this.machine) {
            this.machine.cpuPortWrite(port, value);
            return;
        }
        
        if (this.debug) {
            console.log(`OUT to port 0x${port.toString(16)}, value=0x${value.toString(16)}, size=${size}`);
        }
    }
    
    // Handle string operations
    // 0xA4: MOVSB, 0xA5: MOVSW/MOVSD
    // 0xA6: CMPSB, 0xA7: CMPSW/CMPSD
    // 0xAA: STOSB, 0xAB: STOSW/STOSD
    // 0xAC: LODSB, 0xAD: LODSW/LODSD
    handleStringOp(opcode) {
        // Determine operand size
        // The "B" variants (0xA4, 0xA6, 0xAA, 0xAC) ALWAYS operate on bytes
        // The non-"B" variants (0xA5, 0xA7, 0xAB, 0xAD) depend on operand size prefix
        let size;
        if (opcode === 0xA4 || opcode === 0xA6 || opcode === 0xAA || opcode === 0xAC || opcode === 0xAE) {
            // Byte operations (MOVSB, CMPSB, STOSB, LODSB, SCASB)
            size = 1;
        } else {
            // Word/Dword operations - depends on operand size prefix
            size = this.prefixes.operandSize ? 2 : 4;
        }
        
        const direction = this.getFlag('DF') ? -size : size;
        
        let count = 1;  // Default: 1 iteration
        if (this.prefixes.rep === 1 || this.prefixes.rep === 2) {
            // REP/REPZ/REPNZ
            count = this.regs.ecx;
        }
        
        let cycles = 0;
        let repDone = false;  // Flag to break out of for loop on rep condition
        
        for (let i = 0; i < count && !repDone; i++) {
            switch (opcode) {
                case 0xA4:  // MOVSB
                case 0xA5: {  // MOVSW/MOVSD
                    const src = this.getReg32('esi');
                    const dst = this.getReg32('edi');
                    const val = this.readMem(src, size);
                    this.writeMem(dst, val, size);
                    this.setReg32('esi', this.getReg32('esi') + direction);
                    this.setReg32('edi', this.getReg32('edi') + direction);
                    cycles += 4;
                    break;
                }
                
                case 0xA6:  // CMPSB
                case 0xA7: {  // CMPSW/CMPSD
                    const src = this.getReg32('esi');
                    const dst = this.getReg32('edi');
                    const val1 = this.readMem(src, size);
                    const val2 = this.readMem(dst, size);
                    const result = (val1 - val2) >>> 0;
                    this.updateArithmeticFlags(result, val1, val2, val1 < val2, size === 2);
                    this.setReg32('esi', this.getReg32('esi') + direction);
                    this.setReg32('edi', this.getReg32('edi') + direction);
                    cycles += 4;
                    
                    // Check REPZ/REPNZ condition
                    if ((this.prefixes.rep === 1 && !this.getFlag('ZF')) ||
                        (this.prefixes.rep === 2 && this.getFlag('ZF'))) {
                        repDone = true;
                    }
                    break;
                }
                
                case 0xAA:  // STOSB
                case 0xAB: {  // STOSW/STOSD
                    const dst = this.getReg32('edi');
                    const val = size === 1 ? (this.regs.eax & 0xFF) : 
                                size === 2 ? (this.regs.eax & 0xFFFF) : this.regs.eax;
                    this.writeMem(dst, val, size);
                    this.setReg32('edi', this.getReg32('edi') + direction);
                    cycles += 3;
                    break;
                }
                
                case 0xAC:  // LODSB
                case 0xAD: {  // LODSW/LODSD
                    const src = this.getReg32('esi');
                    const val = this.readMem(src, size);
                    if (size === 1) {
                        this.regs.eax = (this.regs.eax & 0xFFFFFF00) | (val & 0xFF);
                    } else if (size === 2) {
                        this.regs.eax = (this.regs.eax & 0xFFFF0000) | (val & 0xFFFF);
                    } else {
                        this.regs.eax = val;
                    }
                    this.setReg32('esi', this.getReg32('esi') + direction);
                    cycles += 3;
                    break;
                }

                case 0xAE:  // SCASB
                case 0xAF: {  // SCASW/SCASD
                    const dst = this.getReg32('edi');
                    const memVal = this.readMem(dst, size);
                    let regVal;
                    if (size === 1) {
                        regVal = this.regs.eax & 0xFF;
                    } else if (size === 2) {
                        regVal = this.regs.eax & 0xFFFF;
                    } else {
                        regVal = this.regs.eax;
                    }
                    const result = (regVal - memVal) >>> 0;
                    this.updateArithmeticFlags(result, regVal, memVal, (regVal >>> 0) < (memVal >>> 0), size === 1);
                    this.setReg32('edi', this.getReg32('edi') + direction);
                    cycles += 4;

                    if ((this.prefixes.rep === 1 && !this.getFlag('ZF')) ||
                        (this.prefixes.rep === 2 && this.getFlag('ZF'))) {
                        repDone = true;
                    }
                    break;
                }
            }
            
            // Decrement ECX for REP
            if (this.prefixes.rep === 1 || this.prefixes.rep === 2) {
                this.regs.ecx--;
            }
        }
        
        this.regs.eip++;
        return cycles;
    }
    
    // Handle I/O string instructions (0x6C-0x6F)
    // 0x6C: INSB, 0x6D: INSW/INSD
    // 0x6E: OUTSB, 0x6F: OUTSW/OUTSD
    handleStringIO(opcode) {
        let size;
        if (opcode === 0x6C || opcode === 0x6E) {
            // Byte operations (INSB, OUTSB)
            size = 1;
        } else {
            // Word/Dword operations — depends on operand size prefix
            size = this.prefixes.operandSize ? 2 : 4;
        }
        
        const direction = this.getFlag('DF') ? -size : size;
        const port = this.regs.edx & 0xFFFF;
        
        let count = 1;
        if (this.prefixes.rep === 1 || this.prefixes.rep === 2) {
            count = this.regs.ecx;
        }
        
        let cycles = 0;
        
        for (let i = 0; i < count; i++) {
            if (opcode === 0x6C || opcode === 0x6D) {
                // INSB/INSW/INSD: port → [EDI]
                const addr = this.getReg32('edi');
                const value = this.handleIn(port, size);
                this.writeMem(addr, value, size);
                this.setReg32('edi', this.getReg32('edi') + direction);
            } else {
                // OUTSB/OUTSW/OUTSD: [ESI] → port
                const addr = this.getReg32('esi');
                const value = this.readMem(addr, size);
                this.handleOut(port, value, size);
                this.setReg32('esi', this.getReg32('esi') + direction);
            }
            cycles += 4;
            
            // Decrement ECX for REP
            if (this.prefixes.rep === 1 || this.prefixes.rep === 2) {
                this.regs.ecx--;
            }
        }
        
        this.regs.eip++;
        return cycles;
    }
    
    // Handle Group 6/7 extended opcodes (0x0F 0x00)
    // SLDT (reg=0), STR (reg=1), LLDT (reg=2), LTR (reg=3)
    // VERR (reg=4), VERW (reg=5)
    handleGroup6_7() {
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;
        const rm = modrm & 7;
        
        switch (reg) {
            case 0: {  // SLDT — Store LDTR to r/m16
                // We don't use LDT, so store 0
                if (mod === 3) {
                    const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                    this.regs[rmName] = (this.regs[rmName] & 0xFFFF0000) | 0;
                } else {
                    const addr = this.calculateAddress(modrm);
                    this.writeMem(addr, 0, 2);
                }
                return 2;
            }
            case 1: {  // STR — Store Task Register to r/m16
                // We don't use task switching, so store 0
                if (mod === 3) {
                    const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                    this.regs[rmName] = (this.regs[rmName] & 0xFFFF0000) | 0;
                } else {
                    const addr = this.calculateAddress(modrm);
                    this.writeMem(addr, 0, 2);
                }
                return 2;
            }
            case 2:   // LLDT — Load LDTR (no-op, we don't use LDT)
            case 3: { // LTR — Load Task Register (no-op)
                // Read r/m16 operand but ignore it
                if (mod === 3) {
                    // Register — just consume
                } else {
                    this.calculateAddress(modrm);  // Consume ModRM bytes (SIB+disp)
                }
                return 3;
            }
            case 4:   // VERR — Verify segment for reading
            case 5: { // VERW — Verify segment for writing
                // In flat mode, all segments are accessible, so succeed
                this.setFlag('ZF', 1);  // Set ZF to indicate success
                // Need to read the r/m16 source operand
                if (mod === 3) {
                    // Register — just consume
                } else {
                    this.calculateAddress(modrm);
                }
                return 3;
            }
            default:
                this.triggerException(6, 0);  // #UD — Undefined group 6/7 instruction
                return 10;
        }
    }
    
    // Handle MOV to/from segment registers
    // 0x8C: MOV r/m16, segment (store segment to memory/register)
    // 0x8E: MOV segment, r/m16 (load segment from memory/register)
    handleMovSegReg(opcode) {
        this.regs.eip++;
        const modrm = this.readMem(this.regs.eip, 1);
        this.regs.eip++;
        
        const mod = (modrm >> 6) & 3;
        const reg = (modrm >> 3) & 7;  // Segment register (0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS)
        const rm = modrm & 7;
        
        const segNames = ['es', 'cs', 'ss', 'ds', 'fs', 'gs'];
        if (reg > 5) {
            this.triggerException(6, 0);  // #UD — Invalid segment register
            return 10;
        }
        const segName = segNames[reg];
        
        if (opcode === 0x8C) {
            // MOV r/m16, segment - store segment to r/m16
            const segValue = this.segregs[segName];
            if (mod === 3) {
                // Register operand - but segment registers can't be the R/M in this encoding
                // This is actually MOV r32, segment or MOV r16, segment
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                // Store segment to lower 16 bits of 32-bit register
                this.regs[rmName] = (this.regs[rmName] & 0xFFFF0000) | segValue;
            } else {
                // Memory operand
                const addr = this.calculateAddress(modrm);
                this.writeMem(addr, segValue, 2);
            }
            return 2;
        } else {
            // MOV segment, r/m16 - load segment from r/m16
            let segValue = 0;
            if (mod === 3) {
                // Register operand
                const rmName = ['eax','ecx','edx','ebx','esp','ebp','esi','edi'][rm];
                segValue = this.regs[rmName] & 0xFFFF;
            } else {
                // Memory operand
                const addr = this.calculateAddress(modrm);
                segValue = this.readMem(addr, 2);
            }
            this.segregs[segName] = segValue;
            return 3;  // Segment load = 3 cycles
        }
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