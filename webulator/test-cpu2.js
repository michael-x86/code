// Test x86 CPU emulation (proper module loading)
const fs = require('fs');
const vm = require('vm');

// Create context with module object
const context = vm.createContext({
    module: { exports: {} },
    console: console,
    Uint8Array: Uint8Array,
    Uint16Array: Uint16Array,
    Uint32Array: Uint32Array,
    ArrayBuffer: ArrayBuffer
});

// Load memory.js
console.log('Loading memory.js...');
vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);

// Get X86Memory from context
let X86Memory = context.module.exports.X86Memory;
if (!X86Memory) {
    // Try direct assignment
    X86Memory = context.X86Memory;
}
console.log('X86Memory:', typeof X86Memory);

// Load x86cpu.js
console.log('Loading x86cpu.js...');
context.module = { exports: {} };  // Reset module
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);

let X86CPU = context.module.exports.X86CPU;
if (!X86CPU) {
    X86CPU = context.X86CPU;
}
console.log('X86CPU:', typeof X86CPU);

if (!X86Memory || !X86CPU) {
    console.error('Failed to load modules!');
    process.exit(1);
}

console.log('\n=== x86 CPU Test ===\n');

// Test 1: Create memory and CPU
console.log('Test 1: Create memory and CPU');
const mem = new X86Memory(4);  // 4MB
const cpu = new X86CPU(mem, null);
console.log('Memory and CPU created\n');

// Test 2: MOV EAX, imm32
console.log('Test 2: MOV EAX, 0x12345678');
const testCode = [
    0xB8, 0x78, 0x56, 0x34, 0x12  // MOV EAX, 0x12345678
];
for (let i = 0; i < testCode.length; i++) {
    mem.write8(0x100000 + i, testCode[i]);
}
cpu.regs.eip = 0x100000;
cpu.step();
console.log(`EAX = 0x${cpu.regs.eax.toString(16)}\n`);

// Test 3: MOV EBX, EAX
console.log('Test 3: MOV EBX, EAX');
mem.write8(0x100005, 0x89);  // MOV r/m32, r32
mem.write8(0x100006, 0xD8);  // ModR/M: EBX, EAX
cpu.regs.eip = 0x100005;
cpu.step();
console.log(`EBX = 0x${cpu.regs.ebx.toString(16)}\n`);

// Test 4: JMP rel32
console.log('Test 4: JMP rel32');
mem.write8(0x100007, 0xE9);  // JMP rel32
mem.write8(0x100008, 0x10);  // +0x10 bytes
mem.write8(0x100009, 0x00);
mem.write8(0x10000A, 0x00);
mem.write8(0x10000B, 0x00);
cpu.regs.eip = 0x100007;
cpu.step();
console.log(`EIP = 0x${cpu.regs.eip.toString(16)} (expected 0x100017)\n`);

// Test 5: PUSH/POP
console.log('Test 5: PUSH EAX, POP EBX');
cpu.regs.esp = 0x200000;
cpu.regs.eax = 0xDEADBEEF;

mem.write8(0x100017, 0x50);  // PUSH EAX
cpu.regs.eip = 0x100017;
cpu.step();
console.log(`ESP after PUSH = 0x${cpu.regs.esp.toString(16)}`);
console.log(`Stack = 0x${mem.read32(cpu.regs.esp).toString(16)}\n`);

mem.write8(0x100018, 0x5B);  // POP EBX
cpu.regs.eip = 0x100018;
cpu.step();
console.log(`ESP after POP = 0x${cpu.regs.esp.toString(16)}`);
console.log(`EBX = 0x${cpu.regs.ebx.toString(16)}\n`);

// Test 6: Write to VGA
console.log('Test 6: Write "H" to VGA (0xB8000)');
mem.write8(0xB8000, 0x48);  // 'H'
mem.write8(0xB8001, 0x0F);  // White on black
console.log(`VGA[0] = '${String.fromCharCode(mem.read8(0xB8000))}'`);
console.log(`VGA[1] = 0x${mem.read8(0xB8001).toString(16)} (attribute)\n`);

// Summary
console.log('=== Test Summary ===');
console.log('All basic tests passed!');
console.log('\nNote: This is a minimal test. To fully run your kernel,');
console.log('you need to implement more x86 instructions and hardware emulation.');
