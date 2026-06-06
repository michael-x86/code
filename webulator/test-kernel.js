#!/usr/bin/env node
// Test to load and run the actual OS kernel binary
const fs = require('fs');
const vm = require('vm');

console.log("=== Webulator OS Kernel Test ===\n");

// Create context with module object (required for proper loading)
const context = vm.createContext({
    module: { exports: {} },
    console: console,
    Uint8Array: Uint8Array,
    Uint16Array: Uint16Array,
    Uint32Array: Uint32Array,
    ArrayBuffer: ArrayBuffer
});

// Load memory.js FIRST
console.log('Loading memory.js...');
vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);

// Get X86Memory from context.module.exports
const X86Memory = context.module.exports.X86Memory;
const VGATextDevice = context.module.exports.VGATextDevice;
console.log('X86Memory:', typeof X86Memory);

// Load x86cpu.js SECOND (reuse same module object!)
console.log('\nLoading x86cpu.js...');
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);

// Get X86CPU from context.X86CPU (set by 'this.X86CPU = X86CPU')
const X86CPU = context.X86CPU || context.module.exports;
console.log('X86CPU:', typeof X86CPU);

if (!X86Memory || !X86CPU) {
    console.error('Failed to load modules!');
    process.exit(1);
}

// Create memory and CPU (use 1536MB to accommodate kernel at high addresses)
const mem = new X86Memory(1536);
const cpu = new X86CPU(mem, null);

// Load kernel binary at physical address 0x100000 (1MB)
const kernelPath = process.argv[2] || '/home/janko/dev/code/kernel/kernel.bin';
console.log(`\nLoading kernel from: ${kernelPath}`);

if (!fs.existsSync(kernelPath)) {
    console.error('Kernel binary not found!');
    process.exit(1);
}

const kernelBin = fs.readFileSync(kernelPath);
console.log(`Kernel size: ${kernelBin.length} bytes`);

// Load kernel at 0x100000 (physical address where bootloader puts it)
for (let i = 0; i < kernelBin.length; i++) {
    mem.write8(0x100000 + i, kernelBin[i]);
}

// Find _start entry point - it starts with CLI (opcode 0xFA)
// From disassembly: _start is at offset 0x647 in the binary
const entryPointOffset = 0x647;
const entryPointAddr = 0x100000 + entryPointOffset;

console.log(`Entry point (_start) at offset 0x${entryPointOffset.toString(16)} = physical 0x${entryPointAddr.toString(16)}`);

// Verify the entry point has CLI instruction
const firstByte = mem.read8(entryPointAddr);
if (firstByte !== 0xFA) {
    console.warn(`Warning: Expected CLI (0xFA) at entry point, but found 0x${firstByte.toString(16)}`);
    console.warn('The entry point offset may be incorrect.');
}

// Set up initial state like the bootloader would:
// - Protected mode already entered
// - GDT loaded with flat segments  
// - Kernel loaded at 0x100000
// - EIP should start at _start (0x100647 in this case)

cpu.regs.eip = entryPointAddr;  // Start at _start

// Set up segment registers (flat model)
cpu.segregs.cs = 0x08;  // Code segment selector
cpu.segregs.ds = 0x10;  // Data segment selector  
cpu.segregs.es = 0x10;
cpu.segregs.fs = 0x10;
cpu.segregs.gs = 0x10;
cpu.segregs.ss = 0x10;

// Set up a stack
cpu.regs.esp = 0x200000;  // Some high memory for stack
cpu.regs.ebp = cpu.regs.esp;

// Enable protected mode (PE bit in CR0)
cpu.cregs.cr0 = 0x1;  // Protected mode, no paging yet

// Enable CPU debug mode (verbose, but helpful for debugging)
// cpu.debug = true;

// ============================================================================
// Set up a basic IDT before the kernel starts
// This allows the emulator to dispatch exceptions (like #PF) even if the kernel
// hasn't set up its own IDT yet
// ============================================================================
console.log('\nSetting up basic IDT for exception handling...');

const IDT_BASE = 0x5000;  // Place IDT at physical address 0x5000
const IDT_LIMIT = 0x7FF;  // 256 entries * 8 bytes - 1 = 2047 = 0x7FF

// Create a simple exception handler routine at 0x6000
// This handler will:
// 1. Disable interrupts (CLI)
// 2. Halt the CPU (HLT)
// This is a simple handler that doesn't require complex instruction support
const HANDLER_ADDR = 0x6000;

// Simple handler code (assembly):
// cli    (0xFA) - disable interrupts
// hlt    (0xF4) - halt CPU
const handlerCode = [
    0xFA,                    // cli
    0xF4                     // hlt
];

for (let i = 0; i < handlerCode.length; i++) {
    mem.write8(HANDLER_ADDR + i, handlerCode[i]);
}

// Build IDT entries (256 entries, 8 bytes each)
// Each entry:
//   - Offset 0-15: low 16 bits of handler address
//   - Selector: code segment selector (0x08)
//   - Zero byte: 0x00
//   - Type/Attributes: 0x8E (Present=1, DPL=0, Type=0xE = 32-bit interrupt gate)
//   - Offset 16-31: high 16 bits of handler address

for (let i = 0; i < 256; i++) {
    const entryAddr = IDT_BASE + (i * 8);
    const offset = HANDLER_ADDR;
    
    // Offset 0-15
    mem.write8(entryAddr + 0, offset & 0xFF);
    mem.write8(entryAddr + 1, (offset >> 8) & 0xFF);
    
    // Selector (code segment)
    mem.write8(entryAddr + 2, 0x08);  // Selector low
    mem.write8(entryAddr + 3, 0x00);  // Selector high
    
    // Zero byte
    mem.write8(entryAddr + 4, 0x00);
    
    // Type/Attributes (0x8E = Present, DPL=0, 32-bit interrupt gate)
    mem.write8(entryAddr + 5, 0x8E);
    
    // Offset 16-31
    mem.write8(entryAddr + 6, (offset >> 16) & 0xFF);
    mem.write8(entryAddr + 7, (offset >> 24) & 0xFF);
}

// Set CPU IDT base and limit
cpu.idtBase = IDT_BASE;
cpu.idtLimit = IDT_LIMIT;

console.log(`IDT set up at 0x${IDT_BASE.toString(16)}, limit 0x${IDT_LIMIT.toString(16)}`);
console.log(`Exception handler at 0x${HANDLER_ADDR.toString(16)}`);

console.log('\nStarting kernel execution...');
console.log('Initial EIP: 0x' + cpu.regs.eip.toString(16));

// Run until we hit an unimplemented opcode or crash
let instructions = 0;
const maxInstructions = 100000;  // Increased from 10000

try {
    while (instructions < maxInstructions && !cpu.halted) {
        const eipBefore = cpu.regs.eip;
        
        // Debug: print first few instructions
        if (instructions < 50) {
            const opcode = mem.read8(cpu.regs.eip);
            const bytes = [];
            for (let i = 0; i < 4; i++) {
                bytes.push(mem.read8(cpu.regs.eip + i).toString(16).padStart(2, '0'));
            }
            console.log(`[${instructions}] EIP=0x${cpu.regs.eip.toString(16)} OP=0x${opcode.toString(16)} bytes=${bytes.join(' ')}`);
        }
        
        try {
            cpu.step();
        } catch (stepError) {
            console.log(`\nError during cpu.step() at instruction ${instructions}:`);
            console.log(`EIP: 0x${cpu.regs.eip.toString(16)}`);
            console.log(`Error: ${stepError.message}`);
            console.log(`Stack: ${stepError.stack}`);
            throw stepError;  // Re-throw to be caught by outer catch
        }
        
        instructions++;
        
        // Check for infinite loop or weird state
        if (cpu.regs.eip === eipBefore && instructions > 1) {
            console.log('Warning: EIP did not advance');
            break;
        }
    }
} catch (e) {
    console.log(`\nException after ${instructions} instructions:`);
    console.log(`EIP: 0x${cpu.regs.eip.toString(16)}`);
    console.log(`Error: ${e.message}`);
    
    // Try to identify the opcode
    const opcode = mem.read8(cpu.regs.eip);
    console.log(`\nOpcode at fault: 0x${opcode.toString(16)} (decimal: ${opcode})`);
    
    // Read a few bytes for context
    process.stdout.write('Bytes: ');
    for (let i = 0; i < 16; i++) {
        process.stdout.write(mem.read8(cpu.regs.eip + i).toString(16).padStart(2, '0') + ' ');
    }
    process.stdout.write('\n');
    
    // Suggest which instruction this might be
    console.log('\nThis opcode might be:');
    const suggestions = {
        0x0F: '0x0F - Two-byte opcode escape (next byte determines instruction)',
        0x80: '0x80 - ADD r/m8, imm8',
        0x81: '0x81 - ADD r/m32, imm32',
        0x83: '0x83 - ADD r/m32, imm8 (sign-extended)',
        0xC1: '0xC1 - Shift r/m32, imm8',
        0xC8: '0xC8 - ENTER',
        0xC9: '0xC9 - LEAVE',
        0x0: '0x00 - ADD r/m8, r8',
        0x8D: '0x8D - LEA (already implemented?)'
    };
    if (suggestions[opcode]) {
        console.log('  ' + suggestions[opcode]);
    }
}

if (cpu.halted) {
    console.log(`\nCPU halted after ${instructions} instructions`);
}

if (instructions >= maxInstructions) {
    console.log(`\nReached max instruction limit (${maxInstructions})`);
}

// Print final state
console.log('\nFinal CPU state:');
console.log(`EAX: 0x${cpu.regs.eax.toString(16)}`);
console.log(`EBX: 0x${cpu.regs.ebx.toString(16)}`);
console.log(`ECX: 0x${cpu.regs.ecx.toString(16)}`);
console.log(`EDX: 0x${cpu.regs.edx.toString(16)}`);
console.log(`ESI: 0x${cpu.regs.esi.toString(16)}`);
console.log(`EDI: 0x${cpu.regs.edi.toString(16)}`);
console.log(`EBP: 0x${cpu.regs.ebp.toString(16)}`);
console.log(`ESP: 0x${cpu.regs.esp.toString(16)}`);
console.log(`EIP: 0x${cpu.regs.eip.toString(16)}`);
console.log(`EFLAGS: 0x${cpu.eflags.toString(16)}`);
console.log(`CR0: 0x${cpu.cregs.cr0.toString(16)}`);
console.log(`CR3: 0x${cpu.cregs.cr3.toString(16)}`);

// Dump page table structure
if (cpu.cregs.cr3) {
    const cr3 = cpu.cregs.cr3 & 0xFFFFF000;
    console.log('\n=== Page Directory at 0x' + cr3.toString(16) + ' ===');
    let pdeCount = 0;
    for (let i = 0; i < 1024; i++) {
        const pde = mem.read32(cr3 + i * 4);
        if (pde !== 0) {
            console.log('PD[' + i + '] (vaddr 0x' + (i * 0x400000).toString(16) + '-0x' + ((i+1)*0x400000-1).toString(16) + ') = 0x' + pde.toString(16) + ' (PS=' + ((pde>>7)&1) + ' present=' + (pde&1) + ' PT_base=0x' + (pde & 0xFFFFF000).toString(16) + ')');
            pdeCount++;
            
            // Dump page table entries for this PDE (if not 4MB page)
            if (!(pde & 0x80) && (pde & 1)) {
                const ptBase = pde & 0xFFFFF000;
                let pteCount = 0;
                for (let j = 0; j < 1024; j++) {
                    const pte = mem.read32(ptBase + j * 4);
                    if (pte !== 0) {
                        if (pteCount === 0) console.log('  PT entries at 0x' + ptBase.toString(16) + ':');
                        if (pteCount < 5) {
                            console.log('  PT[' + j + '] (vaddr 0x' + (i * 0x400000 + j * 0x1000).toString(16) + ') = 0x' + pte.toString(16) + ' (present=' + (pte&1) + ' phys=0x' + (pte&0xFFFFF000).toString(16) + ')');
                        }
                        pteCount++;
                    }
                }
                if (pteCount > 5) console.log('  ... and ' + (pteCount - 5) + ' more PTEs');
                console.log('  Total PTEs: ' + pteCount);
            }
        }
    }
    console.log('Total non-zero PDEs: ' + pdeCount);
}
