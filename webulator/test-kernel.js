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
