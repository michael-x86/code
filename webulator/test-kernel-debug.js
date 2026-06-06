#!/usr/bin/env node
// Test with debug output for CR0 and EAX changes
const fs = require('fs');
const vm = require('vm');

console.log("=== Webulator OS Kernel Test (Debug) ===\n");

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

const mem = new X86Memory(256);
const cpu = new X86CPU(mem, null);
cpu.debug = false;  // Disable verbose debug, we'll track specific registers

const kernelPath = '/home/janko/dev/code/kernel/kernel.bin';
const kernelBin = fs.readFileSync(kernelPath);
console.log(`Kernel size: ${kernelBin.length} bytes`);

for (let i = 0; i < kernelBin.length; i++) {
    mem.write8(0x100000 + i, kernelBin[i]);
}

cpu.regs.eip = 0x100647;
cpu.regs.cs = 0x08;

console.log(`Starting kernel execution from EIP=0x${cpu.regs.eip.toString(16)}...\n`);

let instructionCount = 0;
let lastCR0 = cpu.cregs.cr0;
let lastEAX = cpu.regs.eax;

while (!cpu.halted && instructionCount < 50000) {
    const eipBefore = cpu.regs.eip;
    cpu.step();
    instructionCount++;
    
    // Track CR0 changes
    if (cpu.cregs.cr0 !== lastCR0) {
        console.log(`[${instructionCount}] CR0 changed: 0x${lastCR0.toString(16)} -> 0x${cpu.cregs.cr0.toString(16)} at EIP=0x${eipBefore.toString(16)}`);
        lastCR0 = cpu.cregs.cr0;
    }
    
    // Track EAX changes near the jump target
    if (cpu.regs.eax !== lastEAX && eipBefore >= 0x100680 && eipBefore <= 0x100690) {
        console.log(`[${instructionCount}] EAX changed: 0x${lastEAX.toString(16)} -> 0x${cpu.regs.eax.toString(16)} at EIP=0x${eipBefore.toString(16)}`);
        lastEAX = cpu.regs.eax;
    }
    
    if (instructionCount % 1000 === 0) {
        console.log(`Executed ${instructionCount} instructions, EIP=0x${cpu.regs.eip.toString(16)}`);
    }
    
    if (cpu.halted) break;
}

console.log(`\nCPU halted after ${instructionCount} instructions`);
console.log(`Final CPU state:`);
console.log(`EAX: 0x${cpu.regs.eax.toString(16)}`);
console.log(`EBX: 0x${cpu.regs.ebx.toString(16)}`);
console.log(`EIP: 0x${cpu.regs.eip.toString(16)}`);
console.log(`CR0: 0x${cpu.cregs.cr0.toString(16)}`);
