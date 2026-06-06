// Test x86 CPU emulation
const fs = require('fs');
const path = require('path');

// Load the JavaScript files
eval(fs.readFileSync(path.join(__dirname, 'hardemu/memory.js'), 'utf8'));
eval(fs.readFileSync(path.join(__dirname, 'hardemu/x86cpu.js'), 'utf8'));

// Create memory and CPU
const mem = new X86Memory(4);  // 4MB for testing
const cpu = new X86CPU(mem, null);

console.log('=== x86 CPU Test ===\n');

// Test 1: MOV EAX, imm32
console.log('Test 1: MOV EAX, 0x12345678');
mem.loadBinary([0xB8, 0x78, 0x56, 0x34, 0x12], 0x100000);  // MOV EAX, 0x12345678
cpu.regs.eip = 0x100000;
cpu.step();
console.log(`EAX = 0x${cpu.regs.eax.toString(16)}\n`);

// Test 2: MOV EBX, EAX
console.log('Test 2: MOV EBX, EAX');
mem.write8(0x100005, 0x89);  // MOV r/m32, r32 (89 C3 = MOV EBX, EAX)
cpu.regs.eip = 0x100005;
cpu.step();
console.log(`EBX = 0x${cpu.regs.ebx.toString(16)}\n`);

// Test 3: ADD EAX, EBX
console.log('Test 3: ADD EAX, EBX');
mem.write8(0x100007, 0x01);  // ADD r/m32, r32 (01 D8 = ADD EAX, EBX)
mem.write8(0x100008, 0xD8);
cpu.regs.eip = 0x100007;
cpu.step();
console.log(`EAX = 0x${cpu.regs.eax.toString(16)}`);
console.log(`FLAGS: CF=${cpu.getFlag('CF')}, ZF=${cpu.getFlag('ZF')}, SF=${cpu.getFlag('SF')}\n`);

// Test 4: JMP rel32
console.log('Test 4: JMP rel32');
mem.write8(0x100009, 0xE9);  // JMP rel32
mem.write8(0x10000A, 0x10);
mem.write8(0x10000B, 0x00);
mem.write8(0x10000C, 0x00);
mem.write8(0x10000D, 0x00);  // Jump 0x10 bytes forward
cpu.regs.eip = 0x100009;
cpu.step();
console.log(`EIP = 0x${cpu.regs.eip.toString(16)} (expected 0x100019)\n`);

// Test 5: PUSH/POP
console.log('Test 5: PUSH EAX, POP EBX');
cpu.regs.esp = 0x200000;
cpu.regs.eax = 0xDEADBEEF;
mem.write8(0x100019, 0x50);  // PUSH EAX
cpu.regs.eip = 0x100019;
cpu.step();
console.log(`ESP after PUSH = 0x${cpu.regs.esp.toString(16)}`);
console.log(`Stack value = 0x${mem.read32(cpu.regs.esp).toString(16)}\n`);

mem.write8(0x10001A, 0x58);  // POP EAX
cpu.regs.eip = 0x10001A;
cpu.step();
console.log(`ESP after POP = 0x${cpu.regs.esp.toString(16)}\n`);

// Test 6: VGA write
console.log('Test 6: Write to VGA (0xB8000)');
cpu.regs.edi = 0xB8000;
mem.write8(cpu.regs.edi, 0x48);  // 'H'
mem.write8(cpu.regs.edi + 1, 0x0F);  // White on black
console.log(`VGA[0] = 0x${mem.read8(0xB8000).toString(16)} ('${String.fromCharCode(mem.read8(0xB8000))}')`);
console.log(`VGA[1] = 0x${mem.read8(0xB8001).toString(16)} (attribute)\n`);

// Summary
console.log('=== Test Summary ===');
console.log('CPU registers:', JSON.stringify(cpu.regs, null, 2));
console.log('\nMemory at 0xB8000 (VGA):', mem.read8(0xB8000));
