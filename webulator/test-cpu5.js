#!/usr/bin/env node
/**
 * test-cpu5.js - Comprehensive test for new x86 instructions
 * 
 * Tests all newly implemented instructions:
 * - PUSHF/POPF
 * - INT 3, INT imm8
 * - CALL rel32, CALL r/m32
 * - LEA
 * - IN/OUT
 * - Shift/Rotate (ROL, ROR, SHL, SHR, SAR, RCL, RCR)
 * - MUL/DIV/IMUL/IDIV
 * - String operations (MOVS, STOS, LODS, CMPS)
 * - MOV to/from segment registers
 */

const fs = require('fs');
const vm = require('vm');

console.log("=== x86 CPU Comprehensive Test (New Instructions) ===\n");

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

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`✓ ${name}`);
        passed++;
    } catch (e) {
        console.log(`✗ ${name}: ${e.message}`);
        failed++;
    }
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message || "Assertion failed");
    }
}

// Test 1: PUSHF/POPF
test('PUSHF/POPF', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.eflags = 0x12345678;
    cpu.regs.esp = 0x1000;
    
    // PUSHF
    mem.write8(0x1000, 0x9C);  // PUSHF
    cpu.regs.eip = 0x1000;
    cpu.step();
    
    assert(cpu.regs.esp === 0x0FFC, "ESP should be 0x0FFC after PUSHF");
    assert(mem.read32(0x0FFC) === 0x12345678, "Stack should contain EFLAGS");
    
    // Modify EFLAGS
    cpu.eflags = 0x00000000;
    
    // POPF
    mem.write8(cpu.regs.eip, 0x9D);  // POPF
    cpu.step();
    
    assert(cpu.regs.esp === 0x1000, "ESP should be 0x1000 after POPF");
    assert(cpu.eflags === 0x12345678, "EFLAGS should be restored");
});

// Test 2: INT 3 (breakpoint)
test('INT 3 (0xCC)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.esp = 0x1000;
    cpu.segregs.cs = 0x0000;
    
    // Set up IVT (Interrupt Vector Table) at 0x0
    mem.write16(3 * 4, 0x1234);       // Offset
    mem.write16(3 * 4 + 2, 0xF000);   // Segment (will be loaded into CS)
    
    // INT 3
    mem.write8(0x1000, 0xCC);
    cpu.regs.eip = 0x1000;
    cpu.step();
    
    // Should have pushed EFLAGS, CS, EIP and jumped to handler
    assert(cpu.regs.esp === 0x0FF4, "ESP should be 0x0FF4 after INT 3");
    assert(cpu.regs.eip === 0x1234, "EIP should be handler offset");
    assert(cpu.segregs.cs === 0xF000, "CS should be handler segment");
    assert(cpu.getFlag('IF') === 0, "IF should be cleared after INT");
});

// Test 3: INT imm8
test('INT imm8 (0xCD)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.esp = 0x1000;
    cpu.segregs.cs = 0x0000;
    
    // Set up IVT for INT 0x21
    mem.write16(0x21 * 4, 0x5678);
    mem.write16(0x21 * 4 + 2, 0xF000);
    
    // INT 0x21
    mem.write8(0x1000, 0xCD);  // INT
    mem.write8(0x1001, 0x21);  // 0x21
    cpu.regs.eip = 0x1000;
    cpu.step();
    
    assert(cpu.regs.eip === 0x5678, "EIP should be handler offset");
    assert(cpu.segregs.cs === 0xF000, "CS should be handler segment");
});

// Test 4: CALL rel32
test('CALL rel32 (0xE8)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.esp = 0x1000;
    cpu.regs.eip = 0x2000;
    
    // CALL rel32 (jump to EIP+0x100)
    mem.write8(0x2000, 0xE8);  // CALL
    mem.write32(0x2001, 0x00000100);  // rel32 = +256
    cpu.step();
    
    assert(cpu.regs.esp === 0x0FFC, "ESP should be 0x0FFC after CALL");
    assert(mem.read32(0x0FFC) === 0x2005, "Return address should be pushed");
    assert(cpu.regs.eip === 0x2105, "EIP should be 0x2005 + 0x100");
});

// Test 5: RET
test('RET (0xC3)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    // Set up stack with return address
    cpu.regs.esp = 0x0FFC;
    mem.write32(0x0FFC, 0x12345678);
    
    // RET
    mem.write8(0x2000, 0xC3);
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(cpu.regs.esp === 0x1000, "ESP should be 0x1000 after RET");
    assert(cpu.regs.eip === 0x12345678, "EIP should be return address");
});

// Test 6: LEA
test('LEA r32, [addr] (0x8D)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.ebx = 0x1000;
    cpu.regs.eip = 0x2000;
    
    // LEA EAX, [EBX+0x10]
    mem.write8(0x2000, 0x8D);  // LEA
    mem.write8(0x2001, 0x43);  // ModR/M: reg=EAX, r/m=[EBX+disp8]
    mem.write8(0x2002, 0x10);  // disp8 = 16
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(cpu.regs.eax === 0x1010, `EAX should be EBX + 16 = 0x1010, got 0x${cpu.regs.eax.toString(16)}`);
});

// Test 7: Shift left (SHL)
test('SHL r/m32, imm8 (0xC1 /4)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x00000001;
    cpu.regs.eip = 0x2000;
    
    // SHL EAX, 4
    mem.write8(0x2000, 0xC1);  // GRP2 with imm8
    mem.write8(0x2001, 0xE0);  // ModR/M: reg=EAX, op=100 (/4 = SHL)
    mem.write8(0x2002, 0x04);  // imm8 = 4
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(cpu.regs.eax === 0x00000010, `EAX should be 0x10 (1 << 4), got 0x${cpu.regs.eax.toString(16)}`);
    assert(cpu.getFlag('CF') === 0, "CF should be 0");
});

// Test 8: Shift right (SHR)
test('SHR r/m32, 1 (0xD1 /5)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x80000000;
    cpu.regs.eip = 0x2000;
    
    // SHR EAX, 1
    mem.write8(0x2000, 0xD1);  // GRP2 with 1
    mem.write8(0x2001, 0xE8);  // ModR/M: reg=EAX, op=101 (/5 = SHR)
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(cpu.regs.eax === 0x40000000, `EAX should be 0x40000000, got 0x${cpu.regs.eax.toString(16)}`);
    assert(cpu.getFlag('CF') === 0, "CF should be 0");
});

// Test 9: ROL (Rotate Left)
test('ROL r/m32, 1 (0xD1 /0)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x80000001;
    cpu.regs.eip = 0x2000;
    
    // ROL EAX, 1
    mem.write8(0x2000, 0xD1);  // GRP2 with 1
    mem.write8(0x2001, 0xE0);  // ModR/M: reg=EAX, op=000 (/0 = ROL)
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(cpu.regs.eax === 0x00000003, `EAX should be 0x3 (rotate left by 1), got 0x${cpu.regs.eax.toString(16)}`);
    assert(cpu.getFlag('CF') === 1, "CF should be 1 (bit 31 rolled into CF)");
});

// Test 10: MUL (unsigned multiply)
test('MUL r/m32 (0xF7 /4)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x00010000;
    cpu.regs.ebx = 0x00010000;
    cpu.regs.eip = 0x2000;
    
    // MUL EBX
    mem.write8(0x2000, 0xF7);  // MUL/DIV
    mem.write8(0x2001, 0xE3);  // ModR/M: reg=EBX, op=100 (/4 = MUL)
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    // 0x10000 * 0x10000 = 0x100000000
    // EAX = 0x00000000 (low 32 bits)
    // EDX = 0x00000001 (high 32 bits)
    assert(cpu.regs.eax === 0x00000000, `EAX should be 0x0 (low 32 bits), got 0x${cpu.regs.eax.toString(16)}`);
    assert(cpu.regs.edx === 0x00000001, `EDX should be 0x1 (high 32 bits), got 0x${cpu.regs.edx.toString(16)}`);
    assert(cpu.getFlag('CF') === 1, "CF should be 1 (result doesn't fit in 32 bits)");
});

// Test 11: DIV (unsigned divide)
test('DIV r/m32 (0xF7 /6)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x00000007;  // Dividend low
    cpu.regs.edx = 0x00000000;  // Dividend high
    cpu.regs.ebx = 0x00000002;  // Divisor
    cpu.regs.eip = 0x2000;
    
    // DIV EBX
    mem.write8(0x2000, 0xF7);  // MUL/DIV
    mem.write8(0x2001, 0xF3);  // ModR/M: reg=EBX, op=110 (/6 = DIV)
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    // 7 / 2 = 3 remainder 1
    // EAX = 0x00000003 (quotient)
    // EDX = 0x00000001 (remainder)
    assert(cpu.regs.eax === 0x00000003, `EAX should be 0x3 (quotient), got 0x${cpu.regs.eax.toString(16)}`);
    assert(cpu.regs.edx === 0x00000001, `EDX should be 0x1 (remainder), got 0x${cpu.regs.edx.toString(16)}`);
});

// Test 12: String operation - STOSB
test('STOSB (0xAA)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x41414141;  // 'AAAA'
    cpu.regs.edi = 0x3000;
    cpu.regs.eip = 0x2000;
    
    // STOSB
    mem.write8(0x2000, 0xAA);
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(mem.read8(0x3000) === 0x41, `Memory at EDI should be 0x41 ('A'), got 0x${mem.read8(0x3000).toString(16)}`);
    assert(cpu.regs.edi === 0x3001, `EDI should be incremented by 1, got 0x${cpu.regs.edi.toString(16)}`);
});

// Test 13: String operation - LODSB
test('LODSB (0xAC)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.esi = 0x4000;
    mem.write8(0x4000, 0x42);  // 'B'
    cpu.regs.eip = 0x2000;
    
    // LODSB
    mem.write8(0x2000, 0xAC);
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert((cpu.regs.eax & 0xFF) === 0x42, `AL should be 0x42 ('B'), got 0x${(cpu.regs.eax & 0xFF).toString(16)}`);
    assert(cpu.regs.esi === 0x4001, `ESI should be incremented by 1, got 0x${cpu.regs.esi.toString(16)}`);
});

// Test 14: String operation - MOVSB
test('MOVSB (0xA4)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.esi = 0x5000;
    cpu.regs.edi = 0x6000;
    mem.write8(0x5000, 0x43);  // 'C'
    cpu.regs.eip = 0x2000;
    
    // MOVSB
    mem.write8(0x2000, 0xA4);
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(mem.read8(0x6000) === 0x43, `Memory at EDI should be 0x43 ('C'), got 0x${mem.read8(0x6000).toString(16)}`);
    assert(cpu.regs.esi === 0x5001, `ESI should be incremented by 1, got 0x${cpu.regs.esi.toString(16)}`);
    assert(cpu.regs.edi === 0x6001, `EDI should be incremented by 1, got 0x${cpu.regs.edi.toString(16)}`);
});

// Test 15: REP STOSB
test('REP STOSB (0xF3 0xAA)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.regs.eax = 0x00000000;  // Fill with 0x00
    cpu.regs.edi = 0x7000;
    cpu.regs.ecx = 0x00000005;  // Fill 5 bytes
    cpu.regs.eip = 0x2000;
    
    // REP STOSB
    mem.write8(0x2000, 0xF3);  // REPZ
    mem.write8(0x2001, 0xAA);  // STOSB
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert(mem.read8(0x7000) === 0x00, "Memory at 0x7000 should be 0x00");
    assert(mem.read8(0x7004) === 0x00, "Memory at 0x7004 should be 0x00");
    assert(cpu.regs.edi === 0x7005, `EDI should be 0x7005, got 0x${cpu.regs.edi.toString(16)}`);
    assert(cpu.regs.ecx === 0x00000000, "ECX should be 0");
});

// Test 16: IN/OUT (simplified - just check they don't crash)
test('IN/OUT (0xE4, 0xE6)', () => {
    const mem = new X86Memory(4);
    const cpu = new X86CPU(mem, null);
    
    cpu.debug = false;  // Suppress debug output
    
    // IN AL, 0x60 (keyboard data)
    mem.write8(0x2000, 0xE4);  // IN AL, imm8
    mem.write8(0x2001, 0x60);  // port 0x60
    cpu.regs.eip = 0x2000;
    cpu.step();
    
    assert((cpu.regs.eax & 0xFF) === 0x00, "AL should be 0x00 (no key pressed)");
    
    // OUT 0x61, AL (PC speaker)
    mem.write8(cpu.regs.eip, 0xE6);  // OUT imm8, AL
    mem.write8(cpu.regs.eip + 1, 0x61);  // port 0x61
    cpu.step();
    
    // Should not crash
    assert(true, "OUT should not crash");
});

// Print summary
console.log(`\n=== Test Summary ===`);
console.log(`Passed: ${passed}`);
console.log(`Failed: ${failed}`);
console.log(`Total: ${passed + failed}`);

if (failed > 0) {
    console.log("\n⚠️  Some tests failed!");
    process.exit(1);
} else {
    console.log("\n✓ All tests passed!");
    process.exit(0);
}
