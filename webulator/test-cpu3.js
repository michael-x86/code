// Test x86 CPU emulation (proper module loading)
const fs = require('fs');
const vm = require('vm');

// Create context with module object (DON'T reset between loads!)
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
console.log('module.exports after memory.js:', Object.keys(context.module.exports));

// Get X86Memory from context
const X86Memory = context.module.exports.X86Memory;
const VGATextDevice = context.module.exports.VGATextDevice;
console.log('X86Memory:', typeof X86Memory);
console.log('VGATextDevice:', typeof VGATextDevice);

// Load x86cpu.js SECOND (reuse same module object!)
console.log('\nLoading x86cpu.js...');
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);
console.log('module.exports after x86cpu.js:', Object.keys(context.module.exports));

// Get X86CPU from context
const X86CPU = context.module.exports.X86CPU;
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
// Instruction: 0xB8 + rd (EAX = 0xB8)
const testAddr = 0x100000;
mem.write8(testAddr, 0xB8);  // MOV EAX, imm32
mem.write8(testAddr + 1, 0x78);  // 0x12345678 bytes
mem.write8(testAddr + 2, 0x56);
mem.write8(testAddr + 3, 0x34);
mem.write8(testAddr + 4, 0x12);
cpu.regs.eip = testAddr;
cpu.step();
console.log(`EAX = 0x${cpu.regs.eax.toString(16).padStart(8, '0')} (expected 0x12345678)\n`);

// Test 3: MOV EBX, EAX
console.log('Test 3: MOV EBX, EAX');
mem.write8(testAddr + 5, 0x89);  // MOV r/m32, r32
mem.write8(testAddr + 6, 0xD8);  // ModR/M: EBX, EAX
cpu.regs.eip = testAddr + 5;
cpu.step();
console.log(`EBX = 0x${cpu.regs.ebx.toString(16).padStart(8, '0')} (expected 0x12345678)\n`);

// Test 4: JMP rel32
console.log('Test 4: JMP rel32');
mem.write8(testAddr + 7, 0xE9);  // JMP rel32
mem.write8(testAddr + 8, 0x10);  // +0x10 bytes
mem.write8(testAddr + 9, 0x00);
mem.write8(testAddr + 10, 0x00);
mem.write8(testAddr + 11, 0x00);
cpu.regs.eip = testAddr + 7;
cpu.step();
const expectedEip = testAddr + 7 + 5 + 0x10;  // JMP + 5 bytes + offset
console.log(`EIP = 0x${cpu.regs.eip.toString(16).padStart(8, '0')} (expected 0x${expectedEip.toString(16)})\n`);

// Test 5: PUSH/POP
console.log('Test 5: PUSH EAX, POP EBX');
cpu.regs.esp = 0x200000;
cpu.regs.eax = 0xDEADBEEF;

mem.write8(testAddr + 12, 0x50);  // PUSH EAX
cpu.regs.eip = testAddr + 12;
cpu.step();
console.log(`ESP after PUSH = 0x${cpu.regs.esp.toString(16).padStart(8, '0')} (expected 0x1FFFFC)`);

const stackVal = mem.read32(cpu.regs.esp);
console.log(`Stack value = 0x${stackVal.toString(16).padStart(8, '0')} (expected 0xDEADBEEF)\n`);

mem.write8(testAddr + 13, 0x5B);  // POP EBX
cpu.regs.eip = testAddr + 13;
cpu.step();
console.log(`ESP after POP = 0x${cpu.regs.esp.toString(16).padStart(8, '0')} (expected 0x200000)`);
console.log(`EBX = 0x${cpu.regs.ebx.toString(16).padStart(8, '0')} (expected 0xDEADBEEF)\n`);

// Test 6: Write to VGA
console.log('Test 6: Write "H" to VGA (0xB8000)');
mem.write8(0xB8000, 0x48);  // 'H'
mem.write8(0xB8001, 0x0F);  // White on black
console.log(`VGA[0] = '${String.fromCharCode(mem.read8(0xB8000))}'`);
console.log(`VGA[1] = 0x${mem.read8(0xB8001).toString(16)} (attribute)\n`);

// Summary
console.log('=== Test Summary ===');
console.log('All basic tests completed!');
console.log('\nNote: This is a minimal test. To fully run your kernel,');
console.log('you need to implement more x86 instructions and hardware emulation.');
