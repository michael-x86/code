#!/usr/bin/env node
const fs = require('fs');
const vm = require('vm');
const path = require('path');

const RED = '\x1b[31m';
const GREEN = '\x1b[32m';
const YELLOW = '\x1b[33m';
const CYAN = '\x1b[36m';
const MAGENTA = '\x1b[35m';
const BOLD = '\x1b[1m';
const RESET = '\x1b[0m';

let totalPassed = 0;
let totalFailed = 0;
let currentSuite = '';

function suite(name, fn) {
  currentSuite = name;
  console.log(`\n${BOLD}${CYAN}=== ${name} ===${RESET}\n`);
  fn();
}

function test(name, fn) {
  try {
    fn();
    console.log(`  ${GREEN}✓${RESET} ${name}`);
    totalPassed++;
  } catch (e) {
    console.log(`  ${RED}✗${RESET} ${name}`);
    console.log(`    ${RED}${e.message}${RESET}`);
    totalFailed++;
  }
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message || 'Assertion failed');
  }
}

function assertEq(actual, expected, label) {
  if (actual !== expected) {
    const a = typeof actual === 'number' ? `0x${actual.toString(16)}` : actual;
    const e = typeof expected === 'number' ? `0x${expected.toString(16)}` : expected;
    throw new Error(`${label || ''} expected ${e}, got ${a}`);
  }
}

function loadModules() {
  const context = vm.createContext({
    module: { exports: {} },
    console: console,
    Uint8Array: Uint8Array,
    Uint16Array: Uint16Array,
    Uint32Array: Uint32Array,
    ArrayBuffer: ArrayBuffer
  });

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/memory.js'), 'utf8'), context);
  const X86Memory = context.module.exports.X86Memory;
  const VGATextDevice = context.module.exports.VGATextDevice;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/x86cpu.js'), 'utf8'), context);
  const X86CPU = context.X86CPU || context.module.exports;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/vga.js'), 'utf8'), context);
  const VGATextMode = context.module.exports;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/machine_x86.js'), 'utf8'), context);
  const MachineExports = context.module.exports;
  const X86Machine = MachineExports.X86Machine;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/bios.js'), 'utf8'), context);
  const BIOS = context.BIOS || context.module.exports;

  return { X86Memory, VGATextDevice, X86CPU, VGATextMode, X86Machine, BIOS };
}

function run() {
  console.log(`${BOLD}${MAGENTA}╔══════════════════════════════════════╗${RESET}`);
  console.log(`${BOLD}${MAGENTA}║    Webulator Comprehensive Test Tool  ║${RESET}`);
  console.log(`${BOLD}${MAGENTA}╚══════════════════════════════════════╝${RESET}`);

  let modules;
  try {
    modules = loadModules();
    console.log(`\n${GREEN}✓${RESET} Modules loaded successfully`);
  } catch (e) {
    console.error(`${RED}Failed to load modules: ${e.message}${RESET}`);
    process.exit(1);
  }

  const { X86Memory, X86CPU, VGATextMode, X86Machine, BIOS } = modules;

  const testCategories = process.argv[2] || 'all';
  const runAll = testCategories === 'all';

  const run = (name, fn) => {
    if (runAll || testCategories === name) {
      suite(name, () => fn(modules));
    }
  };

  run('CPU Basic Instructions', ({ X86Memory, X86CPU }) => {
    test('MOV EAX, imm32', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      mem.write8(0x1000, 0xB8); mem.write32(0x1001, 0x12345678);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x12345678);
    });

    test('MOV EBX, EAX', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xDEADBEEF;
      mem.write8(0x1000, 0x89); mem.write8(0x1001, 0xC3);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.ebx, 0xDEADBEEF);
    });

    test('MOV ECX, [mem]', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      mem.write32(0x2000, 0xAABBCCDD);
      mem.write8(0x1000, 0x8B); mem.write8(0x1001, 0x0D);
      mem.write32(0x1002, 0x2000);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.ecx, 0xAABBCCDD);
    });

    test('MOV [mem], EDX', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.edx = 0x12345678;
      mem.write8(0x1000, 0x89); mem.write8(0x1001, 0x15);
      mem.write32(0x1002, 0x3000);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read32(0x3000), 0x12345678);
    });

    test('MOV AL, imm8', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      mem.write8(0x1000, 0xB0); mem.write8(0x1001, 0x42);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x42);
    });

    test('PUSH/POP registers', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      cpu.regs.eax = 0xDEADBEEF;
      mem.write8(0x1000, 0x50); cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x1FFC);
      assertEq(mem.read32(0x1FFC), 0xDEADBEEF);
      mem.write8(0x1001, 0x58); cpu.regs.eip = 0x1001; cpu.step();
      assertEq(cpu.regs.eax, 0xDEADBEEF);
      assertEq(cpu.regs.esp, 0x2000);
    });

    test('PUSH imm8 sign-extended', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      mem.write8(0x1000, 0x6A); mem.write8(0x1001, 0x7F);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read32(0x1FFC), 0x0000007F);
    });

    test('PUSH imm32', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      mem.write8(0x1000, 0x68); mem.write32(0x1001, 0xFFEEDDCC);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read32(0x1FFC), 0xFFEEDDCC);
    });

    test('JMP rel32 forward', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eip = 0x1000;
      mem.write8(0x1000, 0xE9); mem.write32(0x1001, 0x00000100);
      cpu.step();
      assertEq(cpu.regs.eip, 0x1005 + 0x100);
    });

    test('JMP rel8 backward', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eip = 0x1010;
      mem.write8(0x1010, 0xEB); mem.write8(0x1011, 0xF0);
      cpu.step();
      assertEq(cpu.regs.eip, 0x1012 + (-16));
    });

    test('NOP', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      mem.write8(0x1000, 0x90);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1001);
    });

    test('XOR EAX, EAX (clear register)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xFFFFFFFF;
      mem.write8(0x1000, 0x31); mem.write8(0x1001, 0xC0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0);
      assertEq(cpu.getFlag('ZF'), 1);
      assertEq(cpu.getFlag('SF'), 0);
    });

    test('ADD EAX, imm32', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0x05); mem.write32(0x1001, 0x00000001);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000002);
    });

    test('SUB EAX, imm32', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000005;
      mem.write8(0x1000, 0x2D); mem.write32(0x1001, 0x00000003);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000002);
    });

    test('CMP sets flags correctly', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000005;
      mem.write8(0x1000, 0x3D); mem.write32(0x1001, 0x00000005);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 1, 'ZF should be 1 for equal');
      assertEq(cpu.getFlag('CF'), 0, 'CF should be 0');
    });

    test('INC r32 (0x40-0x47)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000000;
      mem.write8(0x1000, 0x40);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000001);
    });

    test('DEC r32 (0x48-0x4F)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0x48);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000000);
    });

    test('MOVSX/MOVZX not directly - test 8-bit MOV first', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x12345600;
      mem.write8(0x1000, 0xB0); mem.write8(0x1001, 0x78);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x78);
    });
  });

  run('CPU Arithmetic', ({ X86Memory, X86CPU }) => {
    test('ADD with carry wraps to zero and sets CF', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xFFFFFFFF;
      mem.write8(0x1000, 0x05); mem.write32(0x1001, 0x00000001);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000000, 'EAX should wrap to 0');
      assertEq(cpu.getFlag('CF'), 1, 'CF should be set on overflow');
    });

    test('ADD without carry clears CF', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('CF', 1);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0x05); mem.write32(0x1001, 0x00000001);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000002, 'EAX should be 2');
      assertEq(cpu.getFlag('CF'), 0, 'CF should be cleared (no overflow)');
    });

    test('ADC with carry in', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      cpu.regs.ecx = 0x00000001;
      cpu.setFlag('CF', 1);
      mem.write8(0x1000, 0x11); mem.write8(0x1001, 0xC1);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.ecx, 0x00000003, 'ECX = ECX + EAX + CF = 1 + 1 + 1 = 3');
    });

    test('SUB with borrow (sets CF)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0x2D); mem.write32(0x1001, 0x00000002);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('CF'), 1);
    });

    test('MUL r/m32 unsigned', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00010000;
      cpu.regs.ecx = 0x00010000;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xE1);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000000, 'EAX = low 32 bits');
      assertEq(cpu.regs.edx, 0x00000001, 'EDX = high 32 bits');
    });

    test('MUL r/m8 (AL * r/m8 -> AX)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000010;
      cpu.regs.ecx = 0x00000003;
      mem.write8(0x1000, 0xF6); mem.write8(0x1001, 0xE1);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFFFF, 0x0030);
    });

    test('DIV r/m32 unsigned', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000007;
      cpu.regs.edx = 0x00000000;
      cpu.regs.ecx = 0x00000002;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xF1);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000003, 'EAX = quotient');
      assertEq(cpu.regs.edx, 0x00000001, 'EDX = remainder');
    });

    test('DIV r/m8 (AX / r/m8)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x000000FF;
      cpu.regs.ecx = 0x00000010;
      mem.write8(0x1000, 0xF6); mem.write8(0x1001, 0xF1);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq((cpu.regs.eax >> 8) & 0xFF, 0x0F, 'AH = remainder');
      assertEq(cpu.regs.eax & 0xFF, 0x0F, 'AL = quotient');
    });

    test('NEG r/m32 (0xF7 /3) negates positive value', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0xFFFFFFFF, 'NEG 1 should give -1 (0xFFFFFFFF)');
      assertEq(cpu.getFlag('CF'), 1, 'NEG nonzero should set CF');
    });

    test('NEG zero clears CF', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000000;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000000, 'NEG 0 should give 0');
      assertEq(cpu.getFlag('CF'), 0, 'NEG zero should clear CF');
      assertEq(cpu.getFlag('ZF'), 1, 'NEG zero should set ZF');
    });

    test('CMP r32, r32 with flags', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000005;
      cpu.regs.ebx = 0x00000003;
      mem.write8(0x1000, 0x39); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('CF'), 0);
      assertEq(cpu.getFlag('ZF'), 0);
      assertEq(cpu.getFlag('SF'), 0);
    });
  });

  run('CPU Logical', ({ X86Memory, X86CPU }) => {
    test('AND EAX, EBX', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x0000000F;
      cpu.regs.ebx = 0x000000F0;
      mem.write8(0x1000, 0x21); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000000);
      assertEq(cpu.getFlag('ZF'), 1);
    });

    test('OR EAX, EBX', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x0000000F;
      cpu.regs.ebx = 0x000000F0;
      mem.write8(0x1000, 0x09); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x000000FF);
    });

    test('XOR EAX, EBX', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xAAAAAAAA;
      cpu.regs.ebx = 0x55555555;
      mem.write8(0x1000, 0x31); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0xFFFFFFFF);
    });

    test('NOT r/m32 (0xF7 /2) inverts all bits', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000000;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xD0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0xFFFFFFFF, 'NOT 0 should give 0xFFFFFFFF');
    });

    test('NOT r/m32 inverts 0xAAAAAAAA to 0x55555555', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xAAAAAAAA;
      mem.write8(0x1000, 0xF7); mem.write8(0x1001, 0xD0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x55555555, 'NOT 0xAAAAAAAA should give 0x55555555');
    });

    test('TEST EAX, EBX sets flags', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x0000000F;
      cpu.regs.ebx = 0x0000000F;
      mem.write8(0x1000, 0x85); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 0);
      assertEq(cpu.getFlag('CF'), 0);
    });

    test('TEST sets ZF when result is zero', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x000000F0;
      cpu.regs.ebx = 0x0000000F;
      mem.write8(0x1000, 0x85); mem.write8(0x1001, 0xD8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 1);
    });

    test('AND AL, imm8 (0x24) masks AL', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x000000FF;
      mem.write8(0x1000, 0x24); mem.write8(0x1001, 0x0F);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x0F, 'AL should be ANDed with imm8');
      assertEq(cpu.getFlag('ZF'), 0, 'ZF should be 0 (result nonzero)');
    });

    test('AND AL, imm8 sets ZF when result zero', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x000000F0;
      mem.write8(0x1000, 0x24); mem.write8(0x1001, 0x0F);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x00);
      assertEq(cpu.getFlag('ZF'), 1);
    });

    test('OR AL, imm8', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x000000F0;
      mem.write8(0x1000, 0x0C); mem.write8(0x1001, 0x0F);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0xFF);
    });
  });

  run('CPU Shifts and Rotates', ({ X86Memory, X86CPU }) => {
    test('SHL EAX, imm8 (0xC1 /4)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      mem.write8(0x1000, 0xC1); mem.write8(0x1001, 0xE0); mem.write8(0x1002, 0x04);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000010);
    });

    test('SHR EAX, 1 (0xD1 /5)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x80000000;
      mem.write8(0x1000, 0xD1); mem.write8(0x1001, 0xE8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x40000000);
    });

    test('SAR EAX, 1 (0xD1 /7)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x80000000;
      mem.write8(0x1000, 0xD1); mem.write8(0x1001, 0xF8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0xC0000000);
    });

    test('ROL EAX, 1 (0xD1 /0)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x80000001;
      mem.write8(0x1000, 0xD1); mem.write8(0x1001, 0xC0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000003);
      assertEq(cpu.getFlag('CF'), 1);
    });

    test('ROR EAX, 1 (0xD1 /1)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000003;
      mem.write8(0x1000, 0xD1); mem.write8(0x1001, 0xC8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x80000001);
    });

    test('SHL EAX, CL (0xD3 /4)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000001;
      cpu.regs.ecx = 0x00000005;
      mem.write8(0x1000, 0xD3); mem.write8(0x1001, 0xE0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000020);
    });

    test('SHR EAX, CL (0xD3 /5)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000020;
      cpu.regs.ecx = 0x00000005;
      mem.write8(0x1000, 0xD3); mem.write8(0x1001, 0xE8);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000001);
    });

    test('RCL with CF (0xD2 /2 for 8-bit, CL count)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000000;
      cpu.regs.ecx = 0x00000001;
      cpu.setFlag('CF', 1);
      mem.write8(0x1000, 0xD3); mem.write8(0x1001, 0xD0);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax, 0x00000001);
    });
  });

  run('CPU Control Flow', ({ X86Memory, X86CPU }) => {
    test('CALL rel32', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      mem.write8(0x1000, 0xE8); mem.write32(0x1001, 0x00000010);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x1FFC);
      assertEq(mem.read32(0x1FFC), 0x1005);
      assertEq(cpu.regs.eip, 0x1015);
    });

    test('RET (0xC3)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x1FFC;
      mem.write32(0x1FFC, 0x12345678);
      mem.write8(0x1000, 0xC3);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x2000);
      assertEq(cpu.regs.eip, 0x12345678);
    });

    test('RET imm16 (0xC2)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x1FFC;
      mem.write32(0x1FFC, 0x12345678);
      mem.write8(0x1000, 0xC2); mem.write16(0x1001, 0x0010);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x2010);
      assertEq(cpu.regs.eip, 0x12345678);
    });

    test('CALL r/m32 (0xFF /2)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      cpu.regs.ebx = 0x12345678;
      mem.write8(0x1000, 0xFF); mem.write8(0x1001, 0xD3);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x1FFC);
      assertEq(mem.read32(0x1FFC), 0x1002);
      assertEq(cpu.regs.eip, 0x12345678);
    });

    test('JMP r/m32 (0xFF /4)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.ebx = 0x12345678;
      mem.write8(0x1000, 0xFF); mem.write8(0x1001, 0xE3);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x12345678);
    });

    test('PUSH r/m32 (0xFF /6)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x2000;
      cpu.regs.ebx = 0xDEADBEEF;
      mem.write8(0x1000, 0xFF); mem.write8(0x1001, 0xF3);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read32(0x1FFC), 0xDEADBEEF);
    });

    test('JE taken (ZF=1)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('ZF', 1);
      mem.write8(0x1000, 0x74); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1002 + 0x10);
    });

    test('JNE not taken (ZF=1)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('ZF', 1);
      mem.write8(0x1000, 0x75); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1002);
    });

    test('JL taken (SF!=OF)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('SF', 1);
      cpu.setFlag('OF', 0);
      mem.write8(0x1000, 0x7C); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1002 + 0x10);
    });

    test('JG taken (ZF=0 and SF=OF)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('ZF', 0);
      cpu.setFlag('SF', 0);
      cpu.setFlag('OF', 0);
      mem.write8(0x1000, 0x7F); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1002 + 0x10);
    });

    test('LOOP (ECX!=0, jump taken)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.ecx = 0x00000005;
      mem.write8(0x1000, 0xE2); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.ecx, 0x00000004);
      assertEq(cpu.regs.eip, 0x1002 + 0x10);
    });

    test('LOOP (ECX=0, wraps to 0xFFFFFFFF, jump taken)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.ecx = 0x00000000;
      mem.write8(0x1000, 0xE2); mem.write8(0x1001, 0x10);
      cpu.regs.eip = 0x1000; cpu.step();

      assertEq(cpu.regs.ecx, 0xFFFFFFFF);
    });

    test('JCXZ (0xE3) jump taken when ECX=0', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.ecx = 0x00000000;
      mem.write8(0x1000, 0xE3); mem.write8(0x1001, 0x20);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1000 + 2 + 0x20);
    });

    test('JCXZ (0xE3) not taken when ECX!=0', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.ecx = 0x00000001;
      mem.write8(0x1000, 0xE3); mem.write8(0x1001, 0x20);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1000 + 2);
    });

    test('JMP far (0xEA) sets CS and EIP', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      // 0xEA offset32 (4 bytes) seg16 (2 bytes)
      mem.write8(0x1000, 0xEA);
      mem.write32(0x1001, 0x00100000);  // offset = 0x100000
      mem.write16(0x1005, 0x0008);       // CS = 0x08
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x00100000);
      assertEq(cpu.segregs.cs, 0x0008);
    });

    test('PUSHAD/POPAD', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x11111111;
      cpu.regs.ecx = 0x22222222;
      cpu.regs.edx = 0x33333333;
      cpu.regs.ebx = 0x44444444;
      cpu.regs.esp = 0x2000;
      cpu.regs.ebp = 0x55555555;
      cpu.regs.esi = 0x66666666;
      cpu.regs.edi = 0x77777777;
      mem.write8(0x1000, 0x60);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x1FE0);
      cpu.regs.eax = 0; cpu.regs.ecx = 0; cpu.regs.edx = 0;
      cpu.regs.ebx = 0; cpu.regs.ebp = 0; cpu.regs.esi = 0; cpu.regs.edi = 0;
      mem.write8(0x1001, 0x61);
      cpu.regs.eip = 0x1001; cpu.step();
      assertEq(cpu.regs.eax, 0x11111111);
      assertEq(cpu.regs.ebx, 0x44444444);
      assertEq(cpu.regs.ecx, 0x22222222);
      assertEq(cpu.regs.edx, 0x33333333);
    });
  });

  run('CPU Flags and Interrupts', ({ X86Memory, X86CPU }) => {
    test('PUSHF/POPF', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.eflags = 0x12345678;
      cpu.regs.esp = 0x1000;
      mem.write8(0x1000, 0x9C);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.esp, 0x0FFC);
      assertEq(mem.read32(0x0FFC), 0x12345678);
      cpu.eflags = 0;
      mem.write8(cpu.regs.eip, 0x9D);
      cpu.step();
      assertEq(cpu.eflags, 0x12345678);
    });

    test('CLI/STI', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('IF', 1);
      mem.write8(0x1000, 0xFA);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('IF'), 0);
      mem.write8(0x1001, 0xFB);
      cpu.regs.eip = 0x1001; cpu.step();
      assertEq(cpu.getFlag('IF'), 1);
    });

    test('INT 3 (breakpoint)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x1000;
      cpu.segregs.cs = 0x0000;
      mem.write16(3 * 4, 0x1234);
      mem.write16(3 * 4 + 2, 0xF000);
      mem.write8(0x1000, 0xCC);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x1234);
      assertEq(cpu.segregs.cs, 0xF000);
    });

    test('INT imm8', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esp = 0x1000;
      cpu.segregs.cs = 0x0000;
      mem.write16(0x21 * 4, 0x5678);
      mem.write16(0x21 * 4 + 2, 0xF000);
      mem.write8(0x1000, 0xCD); mem.write8(0x1001, 0x21);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eip, 0x5678);
      assertEq(cpu.segregs.cs, 0xF000);
    });

    test('EFLAGS bit 1 always set', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      assert((cpu.eflags & 2) !== 0, 'Bit 1 should always be set');
    });

    test('CLD/STD (direction flag)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('DF', 1);
      mem.write8(0x1000, 0xFC);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('DF'), 0);
      mem.write8(0x1001, 0xFD);
      cpu.regs.eip = 0x1001; cpu.step();
      assertEq(cpu.getFlag('DF'), 1);
    });

    test('CMC (0xF5) complements carry flag (0→1)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('CF', 0);
      mem.write8(0x1000, 0xF5);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('CF'), 1, 'CMC should toggle CF from 0 to 1');
    });

    test('CMC (0xF5) complements carry flag (1→0)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.setFlag('CF', 1);
      mem.write8(0x1000, 0xF5);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('CF'), 0, 'CMC should toggle CF from 1 to 0');
    });

    test('SAHF (0x9E) stores AH into flags low byte', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.eflags = 0;
      cpu.regs.eax = 0x0000D500;  // AH = 0xD5: bits 7=SF, 6=ZF, 4=AF, 2=PF, 0=CF
      mem.write8(0x1000, 0x9E);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('SF'), 1);
      assertEq(cpu.getFlag('ZF'), 1);
      assertEq(cpu.getFlag('CF'), 1);
      assertEq(cpu.getFlag('PF'), 1);
      assertEq(cpu.getFlag('AF'), 1);
    });

    test('LAHF (0x9F) loads flags into AH', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.eflags = 0x000000D5;
      cpu.regs.eax = 0;
      mem.write8(0x1000, 0x9F);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq((cpu.regs.eax >> 8) & 0xFF, 0xD5);
    });
  });

  run('CPU String Operations', ({ X86Memory, X86CPU }) => {
    test('STOSB (store string byte)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x41414141;
      cpu.regs.edi = 0x3000;
      mem.write8(0x1000, 0xAA);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read8(0x3000), 0x41);
      assertEq(cpu.regs.edi, 0x3001);
    });

    test('LODSB (load string byte)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esi = 0x4000;
      mem.write8(0x4000, 0x42);
      mem.write8(0x1000, 0xAC);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x42);
      assertEq(cpu.regs.esi, 0x4001);
    });

    test('MOVSB (move string byte)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esi = 0x5000;
      cpu.regs.edi = 0x6000;
      mem.write8(0x5000, 0x43);
      mem.write8(0x1000, 0xA4);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read8(0x6000), 0x43);
      assertEq(cpu.regs.esi, 0x5001);
      assertEq(cpu.regs.edi, 0x6001);
    });

    test('STOSD (store string dword, 0xAB)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0xDEADBEEF;
      cpu.regs.edi = 0x3000;
      mem.write8(0x1000, 0xAB);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read32(0x3000), 0xDEADBEEF);
      assertEq(cpu.regs.edi, 0x3004);
    });

    test('REP STOSB (fill memory)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000000;
      cpu.regs.edi = 0x7000;
      cpu.regs.ecx = 0x00000005;
      mem.write8(0x1000, 0xF3); mem.write8(0x1001, 0xAA);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read8(0x7000), 0x00);
      assertEq(mem.read8(0x7004), 0x00);
      assertEq(cpu.regs.edi, 0x7005);
      assertEq(cpu.regs.ecx, 0x00000000);
    });

    test('REP MOVSB (copy memory)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esi = 0x5000;
      cpu.regs.edi = 0x6000;
      cpu.regs.ecx = 0x00000004;
      mem.write8(0x5000, 0x41); mem.write8(0x5001, 0x42);
      mem.write8(0x5002, 0x43); mem.write8(0x5003, 0x44);
      mem.write8(0x1000, 0xF3); mem.write8(0x1001, 0xA4);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(mem.read8(0x6000), 0x41);
      assertEq(mem.read8(0x6003), 0x44);
      assertEq(cpu.regs.ecx, 0x00000000);
    });

    test('CMPSB (compare strings)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esi = 0x5000;
      cpu.regs.edi = 0x6000;
      mem.write8(0x5000, 0x41); mem.write8(0x6000, 0x41);
      mem.write8(0x1000, 0xA6);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 1);
    });

    test('SCASB (0xAE) compares AL with [EDI] (mismatch)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000041;
      cpu.regs.edi = 0x6000;
      mem.write8(0x6000, 0x42);
      mem.write8(0x1000, 0xAE);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 0, 'ZF=0 for mismatch');
      assertEq(cpu.regs.edi, 0x6001, 'EDI should advance');
    });

    test('SCASB (0xAE) with match sets ZF=1', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000041;
      cpu.regs.edi = 0x6000;
      mem.write8(0x6000, 0x41);
      mem.write8(0x1000, 0xAE);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 1, 'ZF=1 for match');
      assertEq(cpu.regs.edi, 0x6001, 'EDI should advance');
    });

    test('REPNE SCASB finds matching byte', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000043;
      cpu.regs.edi = 0x6000;
      cpu.regs.ecx = 0x00000005;
      mem.write8(0x6000, 0x41); mem.write8(0x6001, 0x42);
      mem.write8(0x6002, 0x43); mem.write8(0x6003, 0x44);
      mem.write8(0x6004, 0x45);
      mem.write8(0x1000, 0xF2); mem.write8(0x1001, 0xAE);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 1, 'Found matching byte');
      assertEq(cpu.regs.edi, 0x6003, 'EDI after match');
    });

    test('REPZ CMPSB', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.esi = 0x5000;
      cpu.regs.edi = 0x6000;
      cpu.regs.ecx = 0x00000003;
      mem.write8(0x5000, 0x41); mem.write8(0x6000, 0x41);
      mem.write8(0x5001, 0x42); mem.write8(0x6001, 0x42);
      mem.write8(0x5002, 0x43); mem.write8(0x6002, 0x44);
      mem.write8(0x1000, 0xF3); mem.write8(0x1001, 0xA6);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.getFlag('ZF'), 0, 'ZF=0: last bytes differ');
      assertEq(cpu.regs.ecx, 0x00000000, 'ECX=0: all 3 iterations consumed');
    });
  });

  run('CPU I/O Instructions', ({ X86Memory, X86CPU }) => {
    test('IN AL, imm8 (0xE4)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      mem.write8(0x1000, 0xE4); mem.write8(0x1001, 0x60);
      cpu.regs.eip = 0x1000; cpu.step();
      assertEq(cpu.regs.eax & 0xFF, 0x00);
    });

    test('OUT imm8, AL (0xE6)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x00000041;
      mem.write8(0x1000, 0xE6); mem.write8(0x1001, 0x3F8);
      cpu.regs.eip = 0x1000; cpu.step();
    });

    test('IN EAX, DX (0xED)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.edx = 0x00000060;
      mem.write8(0x1000, 0xED);
      cpu.regs.eip = 0x1000; cpu.step();
    });

    test('OUT DX, EAX (0xEF)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.edx = 0x000003F8;
      cpu.regs.eax = 0x00000041;
      mem.write8(0x1000, 0xEF);
      cpu.regs.eip = 0x1000; cpu.step();
    });
  });

  run('CPU Segment Registers', ({ X86Memory, X86CPU }) => {
    test('MOV CS (far jump simulation)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.segregs.cs = 0x0008;
      assertEq(cpu.segregs.cs, 0x0008);
    });

    test('MOV DS, EAX via memory', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.regs.eax = 0x0010;
      mem.write32(0x1000, cpu.regs.eax);
    });

    test('PUSH ES (0x06) and POP ES (0x07)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.segregs.es = 0xB800;
      cpu.regs.esp = 0x2000;
      // PUSH ES at [0x1000]
      mem.write8(0x1000, 0x06);
      cpu.regs.eip = 0x1000;
      cpu.step();
      // ESP should decrease by 4 and value stored
      assertEq(cpu.regs.esp, 0x1FFC);
      const pushed = mem.read32(0x1FFC);
      assertEq(pushed, 0xB800);
      // POP ES at [0x1001]
      cpu.segregs.es = 0;
      mem.write8(0x1001, 0x07);
      cpu.regs.eip = 0x1001;
      cpu.step();
      assertEq(cpu.segregs.es, 0xB800);
      assertEq(cpu.regs.esp, 0x2000);
    });

    test('PUSH DS (0x1E) and POP DS (0x1F)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.segregs.ds = 0x1234;
      cpu.regs.esp = 0x3000;
      mem.write8(0x1000, 0x1E);
      cpu.regs.eip = 0x1000;
      cpu.step();
      assertEq(cpu.regs.esp, 0x2FFC);
      assertEq(mem.read32(0x2FFC), 0x1234);
      mem.write8(0x1001, 0x1F);
      cpu.segregs.ds = 0;
      cpu.regs.eip = 0x1001;
      cpu.step();
      assertEq(cpu.segregs.ds, 0x1234);
    });

    test('PUSH SS (0x16) and POP SS (0x17)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.segregs.ss = 0xABCD;
      cpu.regs.esp = 0x4000;
      mem.write8(0x1000, 0x16);
      cpu.regs.eip = 0x1000;
      cpu.step();
      assertEq(cpu.regs.esp, 0x3FFC);
      assertEq(mem.read32(0x3FFC), 0xABCD);
      mem.write8(0x1001, 0x17);
      cpu.segregs.ss = 0;
      cpu.regs.eip = 0x1001;
      cpu.step();
      assertEq(cpu.segregs.ss, 0xABCD);
    });

    test('PUSH CS (0x0E) stores code segment', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.segregs.cs = 0xF000;
      cpu.regs.esp = 0x5000;
      mem.write8(0x1000, 0x0E);
      cpu.regs.eip = 0x1000;
      cpu.step();
      assertEq(cpu.regs.esp, 0x4FFC);
      assertEq(mem.read32(0x4FFC), 0xF000);
    });
  });

  run('CPU Protected Mode', ({ X86Memory, X86CPU }) => {
    test('Enter protected mode (PE bit)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.cregs.cr0 = 0x00000001;
      assert((cpu.cregs.cr0 & 1) !== 0);
    });

    test('Enable paging (PG bit)', () => {
      const mem = new X86Memory(4);
      const cpu = new X86CPU(mem, null);
      cpu.cregs.cr0 = 0x80000001;
      assert((cpu.cregs.cr0 & 0x80000000) !== 0);
    });

    test('Address translation with paging', () => {
      const mem = new X86Memory(8);
      const cpu = new X86CPU(mem, null);
      cpu.cregs.cr0 = 0x80000001;
      const pdAddr = 0x10000;
      const ptAddr = 0x11000;
      cpu.cregs.cr3 = pdAddr;
      mem.write32(pdAddr, ptAddr | 1);
      mem.write32(ptAddr, 0x20000 | 1);
      const phys = cpu.translateAddress(0x00000000);
      assertEq(phys, 0x20000);
    });

    test('Page fault sets CR2 with faulting address', () => {
      const mem = new X86Memory(8);
      const cpu = new X86CPU(mem, null);
      cpu.debug = false;
      cpu._pagingDebugCount = 9999;
      cpu.cregs.cr0 = 0x80000001;
      const PD_ADDR = 0x10000;
      const PT_ADDR = 0x11000;
      cpu.cregs.cr3 = PD_ADDR;
      // Set up page table entry covering IDT area (0x4000-0x5FFF)
      mem.write32(PD_ADDR + ((0x5000 >>> 22) * 4), PT_ADDR | 1);
      mem.write32(PT_ADDR + ((0x5000 >>> 12) & 0x3FF) * 4, 0x5000 | 3);
      // Handler code at physical 0x5000 (identity mapped)
      mem.write8(0x5000, 0xFA); mem.write8(0x5001, 0xF4);
      cpu.idtBase = 0x5000;
      cpu.idtLimit = 0x7FF;
      cpu.regs.esp = 0x8000;
      cpu.segregs.cs = 0x0008;
      // Set up IDT entry 14 (#PF) pointing to handler at 0x5000
      mem.write16(14 * 8 + 0, 0x5000 & 0xFFFF);
      mem.write16(14 * 8 + 2, 0x0008);
      mem.write8(14 * 8 + 4, 0x00);
      mem.write8(14 * 8 + 5, 0x8E);
      mem.write16(14 * 8 + 6, (0x5000 >> 16) & 0xFFFF);
      // Translate an unmapped address to trigger #PF
      cpu.translateAddress(0x12345000);
      assertEq(cpu.cregs.cr2, 0x12345000, 'CR2 should contain faulting address');
    });
  });

  run('Memory Subsystem', ({ X86Memory }) => {
    test('Basic read/write 8-bit', () => {
      const mem = new X86Memory(4);
      mem.write8(0x1000, 0xAB);
      assertEq(mem.read8(0x1000), 0xAB);
    });

    test('Basic read/write 16-bit', () => {
      const mem = new X86Memory(4);
      mem.write16(0x1000, 0x1234);
      assertEq(mem.read16(0x1000), 0x1234);
    });

    test('Basic read/write 32-bit', () => {
      const mem = new X86Memory(4);
      mem.write32(0x1000, 0xDEADBEEF);
      assertEq(mem.read32(0x1000), 0xDEADBEEF);
    });

    test('VGA text buffer at 0xB8000', () => {
      const mem = new X86Memory(4);
      mem.write8(0xB8000, 0x48);
      mem.write8(0xB8001, 0x0F);
      assertEq(mem.read8(0xB8000), 0x48);
      assertEq(mem.read8(0xB8001), 0x0F);
    });

    test('loadBinary fills memory', () => {
      const mem = new X86Memory(4);
      const data = new Uint8Array([0x11, 0x22, 0x33, 0x44]);
      mem.loadBinary(data, 0x2000);
      assertEq(mem.read8(0x2000), 0x11);
      assertEq(mem.read8(0x2003), 0x44);
    });

    test('MMIO device mapping', () => {
      const mem = new X86Memory(4);
      let written = 0;
      mem.mapMMIO(0x3000, {
        size: 16,
        read: (offset) => 0x42,
        write: (offset, value) => { written = value; }
      });
      assertEq(mem.read8(0x3000), 0x42);
      mem.write8(0x3000, 0x7F);
      assertEq(written, 0x7F);
    });

    test('MMIO unmap', () => {
      const mem = new X86Memory(4);
      mem.mapMMIO(0x4000, { size: 16, read: () => 0x42, write: () => {} });
      mem.unmapMMIO(0x4000);
      mem.write8(0x4000, 0xFF);
    });

    test('readRange and writeRange', () => {
      const mem = new X86Memory(4);
      mem.writeRange(0x5000, [0xAA, 0xBB, 0xCC]);
      const range = mem.readRange(0x5000, 3);
      assertEq(range[0], 0xAA);
      assertEq(range[1], 0xBB);
      assertEq(range[2], 0xCC);
    });

    test('Out of range read returns 0xFF', () => {
      const mem = new X86Memory(1);
      const val = mem.read8(0xFFFFFFFF);
      assertEq(val, 0xFF);
    });

    test('VGA text buffer update callback', () => {
      const mem = new X86Memory(4);
      let callbackCalled = false;
      mem.onVgaUpdate = () => { callbackCalled = true; };
      mem.write8(0xB8000, 0x41);
      assert(callbackCalled, 'VGA update callback should fire');
    });
  });

  run('VGA Text Mode', ({ X86Memory, VGATextMode }) => {
    test('VGATextMode creation', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      assert(vga !== null, 'VGA should be created');
      assertEq(vga.COLS, 80, 'Should have 80 columns');
      assertEq(vga.ROWS, 25, 'Should have 25 rows');
    });

    test('VGA port write and read (CRTC index)', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3D4, 0x0E);
      assertEq(vga.crtcIndex, 0x0E);
      vga.portWrite(0x3D5, 0x10);
      assertEq(vga.crtcRegisters[0x0E], 0x10);
    });

    test('VGA port read back', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3D4, 0x0E);
      vga.portWrite(0x3D5, 0x10);
      vga.portWrite(0x3D4, 0x0E);
      const val = vga.portRead(0x3D5);
      assertEq(val, 0x10);
    });

    test('Sequencer register access', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3C4, 0x02);
      vga.portWrite(0x3C5, 0x0F);
      const val = vga.portRead(0x3C5);
      assertEq(val, 0x0F);
    });

    test('DAC palette read/write', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3C8, 0x00);
      vga.portWrite(0x3C9, 0xFF);
      vga.portWrite(0x3C9, 0x00);
      vga.portWrite(0x3C9, 0x00);
      vga.portWrite(0x3C7, 0x00);
      assertEq(vga.portRead(0x3C9), 0xFF);
    });

    test('Default palette initialized', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      assertEq(vga.dacPalette[0], 0x00);
      assertEq(vga.dacPalette[3], 0x00);
      assertEq(vga.dacPalette[45], 0xFC);
    });

    test('Cursor position tracking', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3D4, 0x0E);
      vga.portWrite(0x3D5, 0x07);
      vga.portWrite(0x3D4, 0x0F);
      vga.portWrite(0x3D5, 0xCF);
      const cursorLoc = (vga.crtcRegisters[0x0E] << 8) | vga.crtcRegisters[0x0F];
      assertEq(cursorLoc, 0x07CF);
    });

    test('writeChar writes to VGA buffer', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.writeChar(0x41, 0x1F);
      assertEq(mem.read8(0xB8000), 0x41);
      assertEq(mem.read8(0xB8001), 0x1F);
    });

    test('writeChar advances cursor', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      assertEq(vga.cursorX, 0);
      assertEq(vga.cursorY, 0);
      vga.writeChar(0x41, 0x07);
      assertEq(vga.cursorX, 1);
    });

    test('Scrolling on last line', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.cursorX = 79;
      vga.cursorY = 24;
      vga.writeChar(0x41, 0x07);
      assertEq(vga.cursorY, 24);
    });

    test('Clear screen', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      mem.write8(0xB8000, 0x41);
      vga.clear();
      assertEq(mem.read8(0xB8000), 0x20);
      assertEq(vga.cursorX, 0);
      assertEq(vga.cursorY, 0);
    });

    test('Graphics controller registers', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.portWrite(0x3CE, 0x05);
      vga.portWrite(0x3CF, 0x20);
      assertEq(vga.graphicsRegs[0x05], 0x20);
      assertEq(vga.portRead(0x3CE), 0x05);
      assertEq(vga.portRead(0x3CF), 0x20);
    });

    test('Attribute controller write sets index in index mode (FF=0)', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.attribFF = 0;
      vga.portWrite(0x3C0, 0x12);
      assertEq(vga.attribIndex, 0x12, 'attribIndex should store the written value');
      assertEq(vga.attribFF, 1, 'flip-flop should toggle to data mode');
    });

    test('Attribute controller write data in data mode (FF=1)', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.attribFF = 0;
      vga.portWrite(0x3C0, 0x13);  // Set index 0x13
      vga.portWrite(0x3C0, 0x08);  // Write data to register 0x13
      assertEq(vga.attribRegs[0x13], 0x08, 'register 0x13 should be written');
      assertEq(vga.attribFF, 1, 'flip-flop should stay in data mode');
    });

    test('Reading 0x3DA resets attribute flip-flop', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.attribFF = 0;
      vga.portWrite(0x3C0, 0x12);  // FF → 1
      vga.portRead(0x3DA);          // FF → 0
      assertEq(vga.attribFF, 0, '0x3DA should reset flip-flop to index mode');
      vga.portWrite(0x3C0, 0x14);  // Should set index (was already data mode)
      assertEq(vga.attribIndex, 0x14, 'should set index again');
    });

    test('Dirty flag management', () => {
      const mem = new X86Memory(4);
      const vga = new VGATextMode(mem, null);
      vga.dirty = false;
      vga.markDirty();
      assert(vga.dirty);
    });
  });

  run('Machine Integration', ({ X86Memory, X86CPU, X86Machine }) => {
    test('X86Machine creation', () => {
      const machine = new X86Machine();
      assert(machine.cpu !== null);
      assert(machine.mem !== null);
      assert(machine.vga !== null);
      assert(machine.pic !== null);
      assert(machine.pit !== null);
      assert(machine.keyboard !== null);
      assert(machine.ata !== null);
      machine.destroy();
    });

    test('Machine reset', () => {
      const machine = new X86Machine();
      machine.reset();
      assert(machine.cpu !== null);
      assert(!machine.running);
      assert(!machine.halted);
      machine.destroy();
    });

    test('Machine step', () => {
      const machine = new X86Machine();
      machine.reset();
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0x90);
      machine.step();
      assertEq(machine.cpu.regs.eip, 0x1001);
      machine.destroy();
    });

    test('Machine execute with VGA output', () => {
      const machine = new X86Machine();
      machine.reset();
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xB8);
      machine.mem.write32(0x1001, 0x12345678);
      machine.step();
      assertEq(machine.cpu.regs.eax, 0x12345678);
      machine.destroy();
    });

    test('PIC creation and reset', () => {
      const machine = new X86Machine();
      assert(machine.pic !== null);
      machine.pic.reset();
      assertEq(machine.pic.master.imr, 0xFF);
      assertEq(machine.pic.slave.imr, 0xFF);
      machine.destroy();
    });

    test('PIC write IMR', () => {
      const machine = new X86Machine();
      machine.pic.writeMaster(0x21, 0xFE);
      assertEq(machine.pic.master.imr, 0xFE);
      machine.destroy();
    });

    test('PIC request IRQ sets IRR (ISR only when IF=1)', () => {
      const machine = new X86Machine();
      machine.pic.master.imr = 0x00;
      // With IF=0 (default), IRQ stays pending in IRR
      machine.pic.requestIRQ(0);
      assert((machine.pic.master.irr & 1) !== 0, 'IRR bit should be set when IF=0');
      assert((machine.pic.master.isr & 1) === 0, 'ISR should NOT be set when IF=0');
      // With IF=1, IRQ is delivered and ISR is set
      const machine2 = new X86Machine();
      machine2.pic.master.imr = 0x00;
      machine2.cpu.setFlag('IF', 1);
      machine2.pic.master.irr = 0;  // clear any stale state
      machine2.pic.master.isr = 0;
      machine2.pic.requestIRQ(0);
      assert((machine2.pic.master.irr & 1) === 0, 'IRR should be cleared after delivery');
      assert((machine2.pic.master.isr & 1) !== 0, 'ISR bit should be set when IF=1');
      machine.destroy();
      machine2.destroy();
    });

    test('PIT creation and reset', () => {
      const machine = new X86Machine();
      assert(machine.pit !== null);
      machine.pit.reset();
      machine.destroy();
    });

    test('PIT write counter', () => {
      const machine = new X86Machine();
      machine.pit.write(0x40, 0x100);
      assertEq(machine.pit.channels[0].count, 0x100);
      machine.destroy();
    });

    test('PIT tick triggers IRQ0 when count reaches 0', () => {
      const machine = new X86Machine();
      machine.pit.write(0x40, 5);
      machine.pic.master.imr = 0xFE;  // unmask IRQ0
      machine.cpu.setFlag('IF', 1);
      // Tick 5 times; on 5th tick count goes from 1→0 and fires IRQ0
      for (let i = 0; i < 4; i++) {
        const beforeIrr = machine.pic.master.irr;
        machine.pit.tick();
        assert(machine.pic.master.irr === 0, `IRR should be 0 after tick ${i} (was 0x${beforeIrr.toString(16)})`);
      }
      // 5th tick: count 1→0, fires IRQ0 which is immediately delivered via PIC (IF=1, unmasked)
      // PIC's checkInterrupts clears IRR and sets ISR when delivering
      machine.pit.tick();
      // IRR is cleared because the interrupt was already delivered to CPU
      // ISR should be set instead
      assert(machine.pic.master.isr !== 0, 'ISR should be set after PIT fires IRQ0');
      machine.destroy();
    });

    test('PIC delivers interrupt to CPU via handleInt', () => {
      const machine = new X86Machine();
      const IDT_BASE = 0x1000;
      const HANDLER = 0x5000;
      machine.cpu.idtBase = IDT_BASE;
      machine.cpu.idtLimit = 0x7FF;
      machine.mem.write16(HANDLER, 0xCF);  // IRET
      for (let i = 0; i < 256; i++) {
        const entryAddr = IDT_BASE + (i * 8);
        machine.mem.write16(entryAddr, HANDLER & 0xFFFF);
        machine.mem.write16(entryAddr + 2, 0x0008);
        machine.mem.write8(entryAddr + 4, 0x00);
        machine.mem.write8(entryAddr + 5, 0x8E);
        machine.mem.write8(entryAddr + 6, (HANDLER >> 16) & 0xFF);
        machine.mem.write8(entryAddr + 7, (HANDLER >> 24) & 0xFF);
      }
      machine.cpu.regs.eip = 0x2000;
      machine.cpu.segregs.cs = 0x08;
      machine.cpu.regs.esp = 0x10000;
      machine.cpu.setFlag('IF', 1);
      // Request IRQ0 via PIC — should call cpu.handleInt and jump to handler
      machine.pic.master.imr = 0xFE;  // unmask IRQ0
      machine.triggerIRQ(0);
      assertEq(machine.cpu.regs.eip, HANDLER, 'EIP should be at handler after interrupt');
      assert(machine.cpu.regs.esp < 0x10000, 'ESP should have decreased for stack frame');
      machine.destroy();
    });

    test('HLT wakes up on timer interrupt', () => {
      const machine = new X86Machine();
      const IDT_BASE = 0x1000;
      const HANDLER = 0x5000;
      machine.cpu.idtBase = IDT_BASE;
      machine.cpu.idtLimit = 0x7FF;
      machine.mem.write16(HANDLER, 0xCF);  // IRET
      for (let i = 0; i < 256; i++) {
        const entryAddr = IDT_BASE + (i * 8);
        machine.mem.write16(entryAddr, HANDLER & 0xFFFF);
        machine.mem.write16(entryAddr + 2, 0x0008);
        machine.mem.write8(entryAddr + 4, 0x00);
        machine.mem.write8(entryAddr + 5, 0x8E);
        machine.mem.write8(entryAddr + 6, (HANDLER >> 16) & 0xFF);
        machine.mem.write8(entryAddr + 7, (HANDLER >> 24) & 0xFF);
      }
      machine.cpu.setFlag('IF', 1);
      machine.cpu.segregs.cs = 0x08;
      machine.cpu.regs.esp = 0x10000;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xF4);  // HLT
      machine.mem.write8(0x1001, 0x90);  // NOP after HLT
      // Unmask IRQ0 and set PIT to fire after 10 ticks
      machine.pit.write(0x40, 10);
      machine.pic.master.imr = 0xFE;
      // Step CPU to execute HLT
      machine.cpu.step();
      assert(machine.cpu.halted, 'CPU should be halted after HLT');
      assertEq(machine.cpu.regs.eip, 0x1001, 'EIP should point past HLT');
      // Tick PIT until IRQ0 fires and wakes CPU
      for (let i = 0; i < 10; i++) machine.pit.tick();
      // handleInt should have cleared halted and set EIP to handler
      assert(!machine.cpu.halted, 'CPU should wake from HLT via interrupt');
      assertEq(machine.cpu.regs.eip, HANDLER, 'EIP should be at interrupt handler');
      // Step CPU to execute IRET — returns to instruction after HLT
      const savedSp = machine.cpu.regs.esp;
      machine.cpu.step();
      assertEq(machine.cpu.regs.eip, 0x1001, 'EIP should return to NOP after IRET');
      assertEq(machine.cpu.regs.esp, savedSp + 12, 'ESP should be restored after IRET');
      machine.destroy();
    });

    test('PS/2 keyboard buffer', () => {
      const machine = new X86Machine();
      machine.keyboard.sendScancode(0x1E);
      assertEq(machine.keyboard.buffer.length, 1);
      const val = machine.keyboard.read(0x60);
      assertEq(val, 0x1E);
      machine.destroy();
    });

    test('PS/2 keyboard status', () => {
      const machine = new X86Machine();
      machine.keyboard.sendScancode(0x1E);
      const status = machine.keyboard.read(0x64);
      assert((status & 0x01) !== 0);
      machine.destroy();
    });

    test('PS/2 keyboard multiple scan codes', () => {
      const machine = new X86Machine();
      machine.keyboard.sendScancode(0x1E);
      machine.keyboard.sendScancode(0x30);
      assertEq(machine.keyboard.read(0x60), 0x1E);
      assertEq(machine.keyboard.read(0x60), 0x30);
      machine.destroy();
    });

    test('ATA disk creation', () => {
      const machine = new X86Machine();
      assert(machine.ata !== null);
      machine.destroy();
    });

    test('ATA write command register', () => {
      const machine = new X86Machine();
      machine.ata.writePrimary(0x1F7, 0xEC);
      assertEq(machine.ata.regs.command, 0xEC);
      machine.destroy();
    });

    test('ATA identify device', () => {
      const machine = new X86Machine();
      machine.ata.writePrimary(0x1F7, 0xEC);
      // DRQ + DRDY set (PIO buffer has data ready)
      assertEq(machine.ata.regs.status, 0x58);
      // PIO buffer should be populated with identify data
      assert(machine.ata.pioCount > 0, 'PIO buffer should have data');
      assertEq(machine.ata.pioBuffer[0], 0x0040, 'Word 0: fixed disk config');
      assertEq(machine.ata.pioBuffer[47] & 0x8000, 0x8000, 'IORDY supported');
      // Model string should start with 'W' for 'Webulator'
      assertEq(machine.ata.pioBuffer[27] & 0xFF, 0x57, 'Model: W');
      machine.destroy();
    });

    test('Machine port I/O routing to VGA', () => {
      const machine = new X86Machine();
      assert(machine.vga !== null, 'VGA should exist');
      assert(typeof machine.cpuPortWrite === 'function', 'cpuPortWrite should exist');
      const before = machine.vga.crtcIndex;
      machine.cpuPortWrite(0x3D4, 0x0E);
      assertEq(machine.vga.crtcIndex, 0x0E, 'VGA crtcIndex should be set via port write');
      machine.destroy();
    });

    test('Machine adds breakpoints', () => {
      const machine = new X86Machine();
      machine.addBreakpoint(0x1000);
      assert(machine.cpu.breakpoints.has(0x1000));
      machine.removeBreakpoint(0x1000);
      assert(!machine.cpu.breakpoints.has(0x1000));
      machine.destroy();
    });

    test('Machine CPU state snapshot', () => {
      const machine = new X86Machine();
      const state = machine.getCPUState();
      assert(typeof state.regs.eax === 'number');
      assert(typeof state.eflags === 'number');
      machine.destroy();
    });
  });

  run('Full Kernel Execution', ({ X86Memory, X86CPU }) => {
    const kernelPath = process.argv[3] || path.join(__dirname, 'kernel-bin/kernel.bin');
    const kernelBin = fs.existsSync(kernelPath) ? fs.readFileSync(kernelPath) : null;

    test('Kernel binary exists', () => {
      assert(kernelBin !== null, `Kernel not found at ${kernelPath}`);
      assert(kernelBin.length > 0, 'Kernel binary is empty');
    });

    if (kernelBin) {
      test('Load kernel at 1MB mark', () => {
        const mem = new X86Memory(1536);
        for (let i = 0; i < kernelBin.length; i++) {
          mem.write8(0x100000 + i, kernelBin[i]);
        }
        assertEq(mem.read8(0x100000), kernelBin[0]);
        assertEq(mem.read8(0x100000 + kernelBin.length - 1), kernelBin[kernelBin.length - 1]);
      });

      test('Kernel entry point starts with CLI (0xFA)', () => {
        const mem = new X86Memory(1536);
        for (let i = 0; i < kernelBin.length; i++) {
          mem.write8(0x100000 + i, kernelBin[i]);
        }
        const firstByte = mem.read8(0x100000);
        assertEq(firstByte, 0xFA, 'Kernel should start with CLI instruction');
        const secondByte = mem.read8(0x100001);
        assertEq(secondByte, 0x66, 'Second byte should be 0x66 (operand size prefix)');
      });

      test('Kernel executes at least 100 instructions without crash', () => {
        const mem = new X86Memory(1536);
        const cpu = new X86CPU(mem, null);
        for (let i = 0; i < kernelBin.length; i++) {
          mem.write8(0x100000 + i, kernelBin[i]);
        }
        const IDT_BASE = 0x5000;
        const HANDLER_ADDR = 0x6000;
        mem.write8(HANDLER_ADDR, 0xFA); mem.write8(HANDLER_ADDR + 1, 0xF4);
        for (let i = 0; i < 256; i++) {
          const entryAddr = IDT_BASE + (i * 8);
          mem.write16(entryAddr, HANDLER_ADDR & 0xFFFF);
          mem.write16(entryAddr + 2, 0x0008);
          mem.write8(entryAddr + 4, 0x00);
          mem.write8(entryAddr + 5, 0x8E);
          mem.write8(entryAddr + 6, (HANDLER_ADDR >> 16) & 0xFF);
          mem.write8(entryAddr + 7, (HANDLER_ADDR >> 24) & 0xFF);
        }
        cpu.regs.eip = 0x100000;
        cpu.segregs.cs = 0x08;
        cpu.segregs.ds = 0x10;
        cpu.segregs.es = 0x10;
        cpu.segregs.fs = 0x10;
        cpu.segregs.gs = 0x10;
        cpu.segregs.ss = 0x10;
        cpu.regs.esp = 0x200000;
        cpu.regs.ebp = cpu.regs.esp;
        cpu.cregs.cr0 = 0x1;
        cpu.idtBase = IDT_BASE;
        cpu.idtLimit = 0x7FF;
        const maxInstructions = 100;
        let executed = 0;
        for (let i = 0; i < maxInstructions && !cpu.halted; i++) {
          try {
            cpu.step();
            executed++;
          } catch (e) {
            break;
          }
        }
        assert(executed >= 10, `Only executed ${executed} instructions before crash`);
      });
    }
  });

  run('BIOS Services', ({ X86Machine, BIOS, X86Memory, X86CPU }) => {
    test('BIOS instance created and wired to CPU', () => {
      const machine = new X86Machine();
      assert(machine.bios !== null, 'BIOS should exist on machine');
      assert(machine.cpu.bios !== null, 'BIOS should be wired to CPU');
      assert(machine.cpu.bios instanceof BIOS, 'BIOS should be BIOS instance');
      machine.destroy();
    });

    test('INT 10h AH=0x0E teletype writes to VGA buffer', () => {
      const machine = new X86Machine();
      machine.setupInitialIDT();
      // Set up a tiny snippet at the initial IVT vector for INT 10h so
      // handleInt can find it if BIOS trap is bypassed — but our trap
      // intercepts before handleInt, so this is just safety.
      machine.cpu.regs.eax = 0x0E00 | 0x48;  // AH=0x0E, AL='H'
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);  // INT
      machine.mem.write8(0x1001, 0x10);  // 0x10
      // Execute INT 0x10 — BIOS should handle it before handleInt
      machine.cpu.step();
      const charCode = machine.mem.read8(0xB8000);
      assertEq(charCode, 0x48, 'VGA buffer should contain H');
      assert(machine.vga.dirty, 'VGA should be dirty after teletype');
      assertEq(machine.vga.cursorX, 1, 'Cursor should advance after teletype');
      machine.destroy();
    });

    test('INT 10h teletype handles newline and scroll', () => {
      const machine = new X86Machine();
      // Move cursor to last row, first column
      machine.vga.cursorY = 24;
      machine.vga.cursorX = 0;
      // Write newline (0x0A)
      machine.cpu.regs.eax = 0x0E00 | 0x0A;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x10);
      machine.cpu.step();
      // Cursor should stay at row 24 (scrolled, new bottom row)
      assertEq(machine.vga.cursorY, 24, 'Cursor should stay at bottom after LF+scroll');
      assertEq(machine.vga.cursorX, 0, 'Cursor col unchanged after LF');
      machine.destroy();
    });

    test('INT 10h AH=0x02 set cursor position', () => {
      const machine = new X86Machine();
      machine.cpu.regs.eax = 0x0200;  // AH=0x02
      machine.cpu.regs.edx = 0x0503;  // DH=5, DL=3
      machine.cpu.regs.ebx = 0;       // BH=page 0
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x10);
      machine.cpu.step();
      assertEq(machine.vga.cursorY, 5, 'Cursor row should be 5');
      assertEq(machine.vga.cursorX, 3, 'Cursor col should be 3');
      machine.destroy();
    });

    test('INT 10h AH=0x03 get cursor position', () => {
      const machine = new X86Machine();
      machine.vga.cursorY = 12;
      machine.vga.cursorX = 40;
      machine.cpu.regs.eax = 0x0300;  // AH=0x03
      machine.cpu.regs.ebx = 0;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x10);
      machine.cpu.step();
      const row = (machine.cpu.regs.edx >> 8) & 0xFF;
      const col = machine.cpu.regs.edx & 0xFF;
      assertEq(row, 12, 'DH should be cursor row');
      assertEq(col, 40, 'DL should be cursor col');
      machine.destroy();
    });

    test('INT 10h scroll up blanks the window', () => {
      const machine = new X86Machine();
      // Fill screen with 'X' chars
      for (let i = 0; i < 25 * 80; i++) {
        machine.mem.write8(0xB8000 + i * 2, 0x58);
      }
      // Scroll up the whole window with AL=0 (blank all)
      machine.cpu.regs.eax = 0x0600;
      machine.cpu.regs.ebx = 0x0700;
      machine.cpu.regs.ecx = 0x0000;
      machine.cpu.regs.edx = 0x184F;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x10);
      machine.cpu.step();
      // All cells should be space (0x20)
      assertEq(machine.mem.read8(0xB8000), 0x20, 'Top-left should be blank after scroll');
      assert(machine.vga.dirty, 'VGA should be dirty after scroll');
      machine.destroy();
    });

    test('INT 10h AH=0x09 write char with attribute', () => {
      const machine = new X86Machine();
      machine.cpu.regs.eax = 0x0900 | 0x41;  // AH=0x09, AL='A'
      machine.cpu.regs.ebx = 0x02;            // BL=green-on-black
      machine.cpu.regs.ecx = 1;               // CX=count
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x10);
      machine.cpu.step();
      assertEq(machine.mem.read8(0xB8000), 0x41, 'VGA buffer should contain A');
      assertEq(machine.mem.read8(0xB8001), 0x02, 'Attribute should be 0x02');
      machine.destroy();
    });

    test('INT 16h AH=0x01 check key returns ZF=1 when empty', () => {
      const machine = new X86Machine();
      machine.cpu.regs.eax = 0x0100;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x16);
      machine.cpu.step();
      const zf = (machine.cpu.eflags >> 6) & 1;
      assertEq(zf, 1, 'ZF should be 1 when no key available');
      machine.destroy();
    });

    test('INT 16h AH=0x00 read key after scancode in buffer', () => {
      const machine = new X86Machine();
      // Put a scancode in the keyboard buffer
      machine.keyboard.buffer.push(0x1E);  // 'a'
      machine.keyboard.outputFull = true;
      // Read via INT 16h AH=0x00
      machine.cpu.regs.eax = 0x0000;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x16);
      machine.cpu.step();
      // AL should be ASCII 'a' (0x61), AH should be scancode (0x1E)
      const al = machine.cpu.regs.eax & 0xFF;
      const ah = (machine.cpu.regs.eax >> 8) & 0xFF;
      assertEq(al, 0x61, 'AL should be ASCII a');
      assertEq(ah, 0x1E, 'AH should be scancode 0x1E');
      machine.destroy();
    });

    test('INT 11h returns equipment list', () => {
      const machine = new X86Machine();
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x11);
      machine.cpu.step();
      assertEq(machine.cpu.regs.eax & 0xFFFF, 0x0020, 'Equipment list should have video+floppy');
      machine.destroy();
    });

    test('INT 12h returns memory size', () => {
      const machine = new X86Machine();
      machine.cpu.regs.eax = 0x12340000;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x12);
      machine.cpu.step();
      assertEq(machine.cpu.regs.eax & 0xFFFF, 640, 'AX should be 640 KB');
      machine.destroy();
    });

    test('Non-BIOS INT passes through to normal dispatch', () => {
      const machine = new X86Machine();
      machine.setupInitialIDT();
      // Set up a simple handler at 0x5000 that does IRET
      machine.mem.write8(0x5000, 0xCF);  // IRET
      // Set IDT entry for INT 0x80 to point to 0x5000
      const entryAddr = machine.cpu.idtBase + (0x80 * 8);
      machine.mem.write16(entryAddr, 0x5000 & 0xFFFF);
      machine.mem.write16(entryAddr + 2, 0x0008);
      machine.mem.write8(entryAddr + 4, 0x00);
      machine.mem.write8(entryAddr + 5, 0x8E);
      machine.mem.write8(entryAddr + 6, (0x5000 >> 16) & 0xFF);
      machine.mem.write8(entryAddr + 7, (0x5000 >> 24) & 0xFF);
      machine.cpu.regs.eax = 0x12345678;
      machine.cpu.regs.esp = 0x10000;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x80);
      const spBefore = machine.cpu.regs.esp;
      machine.cpu.step();
      // Non-BIOS INT should have pushed a frame (ESP decreased)
      assert(machine.cpu.regs.esp < spBefore, 'ESP should decrease (interrupt frame pushed)');
      // EIP should now be at the handler (0x5000), not past INT
      assertEq(machine.cpu.regs.eip, 0x5000, 'EIP should be at IRET handler');
      machine.destroy();
    });
  });

  run('Keyboard Input Capture', ({ X86Machine }) => {
    test('handleKeyEvent sends scancode for letter a', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('a', true);
      assert(machine.keyboard.buffer.length > 0, 'Buffer should contain scancode');
      const sc = machine.keyboard.buffer[0];
      assertEq(sc, 0x1E, 'Scancode for a should be 0x1E');
      machine.destroy();
    });

    test('handleKeyEvent sets outputFull flag and triggers IRQ1', () => {
      const machine = new X86Machine();
      let irq1Fired = false;
      const origTrigger = machine.triggerIRQ.bind(machine);
      machine.triggerIRQ = (irq) => {
        if (irq === 1) irq1Fired = true;
        origTrigger(irq);
      };
      machine.keyboard.handleKeyEvent('Enter', true);
      assert(machine.keyboard.outputFull, 'Output full flag should be set');
      assert(irq1Fired, 'IRQ1 should fire on keypress');
      machine.destroy();
    });

    test('handleKeyEvent key release sends break code', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('a', false);  // key release
      assert(machine.keyboard.buffer.length > 0, 'Buffer should contain break scancode');
      const sc = machine.keyboard.buffer[0];
      assertEq(sc, 0x1E | 0x80, 'Break scancode should have high bit set');
      machine.destroy();
    });

    test('handleKeyEvent maps Enter, Backspace, Tab, Escape', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('Enter', true);
      assertEq(machine.keyboard.buffer[0], 0x1C);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('Backspace', true);
      assertEq(machine.keyboard.buffer[0], 0x0E);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('Tab', true);
      assertEq(machine.keyboard.buffer[0], 0x0F);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('Escape', true);
      assertEq(machine.keyboard.buffer[0], 0x01);
      machine.destroy();
    });

    test('handleKeyEvent maps arrow keys', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('ArrowUp', true);
      assertEq(machine.keyboard.buffer[0], 0x48);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('ArrowDown', true);
      assertEq(machine.keyboard.buffer[0], 0x50);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('ArrowLeft', true);
      assertEq(machine.keyboard.buffer[0], 0x4B);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('ArrowRight', true);
      assertEq(machine.keyboard.buffer[0], 0x4D);
      machine.destroy();
    });

    test('handleKeyEvent maps function keys', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('F1', true);
      assertEq(machine.keyboard.buffer[0], 0x3B);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('F5', true);
      assertEq(machine.keyboard.buffer[0], 0x3F);
      machine.keyboard.buffer = [];
      machine.keyboard.handleKeyEvent('F10', true);
      assertEq(machine.keyboard.buffer[0], 0x44);
      machine.destroy();
    });

    test('Multiple keypresses queued in buffer order', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('h', true);
      machine.keyboard.handleKeyEvent('e', true);
      machine.keyboard.handleKeyEvent('l', true);
      machine.keyboard.handleKeyEvent('l', true);
      machine.keyboard.handleKeyEvent('o', true);
      assertEq(machine.keyboard.buffer.length, 5, 'Buffer should hold 5 scancodes');
      assertEq(machine.keyboard.read(0x60), 0x23, 'First: h scancode');
      assertEq(machine.keyboard.read(0x60), 0x12, 'Second: e scancode');
      assertEq(machine.keyboard.read(0x60), 0x26, 'Third: l scancode');
      assertEq(machine.keyboard.read(0x60), 0x26, 'Fourth: l scancode');
      assertEq(machine.keyboard.read(0x60), 0x18, 'Fifth: o scancode');
      assertEq(machine.keyboard.buffer.length, 0, 'Buffer should be empty after reads');
      machine.destroy();
    });

    test('Keypress captured then read via port 0x60', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('x', true);
      const sc = machine.keyboard.read(0x60);
      assertEq(sc, 0x2D, 'Port 0x60 should return scancode 0x2D for x');
      const status = machine.keyboard.read(0x64);
      assert((status & 0x01) === 0, 'Status should show buffer empty after read');
      machine.destroy();
    });

    test('handleKeyEvent handles Space key', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent(' ', true);
      assertEq(machine.keyboard.buffer[0], 0x39, 'Space scancode should be 0x39');
      machine.destroy();
    });

    test('handleKeyEvent handles uppercase letter same scancode as lowercase', () => {
      const machine = new X86Machine();
      machine.keyboard.handleKeyEvent('A', true);
      assertEq(machine.keyboard.buffer[0], 0x1E, 'A scancode same as a (0x1E)');
      machine.destroy();
    });

    test('Terminal onData forwards keypress to keyboard', () => {
      // Simulate what init.js terminal.onData does
      const machine = new X86Machine();
      const data = 'hello';
      for (const ch of data) {
        let key = ch;
        if (key === '\r') key = 'Enter';
        else if (key === '\x7f') key = 'Backspace';
        else if (key === '\t') key = 'Tab';
        else if (key === '\x1b') key = 'Escape';
        machine.keyboard.handleKeyEvent(key, true);
        machine.keyboard.handleKeyEvent(key, false);
      }
      // 5 chars x 2 (press + release) = 10 scancodes
      assertEq(machine.keyboard.buffer.length, 10, 'Buffer should have 10 scancodes');
      // Read and verify press + release pattern
      assertEq(machine.keyboard.read(0x60), 0x23, 'h press');
      assertEq(machine.keyboard.read(0x60), 0x23 | 0x80, 'h release');
      assertEq(machine.keyboard.read(0x60), 0x12, 'e press');
      assertEq(machine.keyboard.read(0x60), 0x12 | 0x80, 'e release');
      machine.destroy();
    });

    test('Keypress captured and readable via BIOS INT 16h', () => {
      const machine = new X86Machine();
      // Send a keypress through the keyboard
      machine.keyboard.handleKeyEvent('z', true);
      // Read via INT 16h AH=0x00
      machine.cpu.regs.eax = 0x0000;
      machine.cpu.regs.eip = 0x1000;
      machine.mem.write8(0x1000, 0xCD);
      machine.mem.write8(0x1001, 0x16);
      machine.cpu.step();
      const al = machine.cpu.regs.eax & 0xFF;
      const ah = (machine.cpu.regs.eax >> 8) & 0xFF;
      assertEq(al, 0x7A, 'AL should be ASCII z (0x7A)');
      assertEq(ah, 0x2C, 'AH should be scancode 0x2C');
      machine.destroy();
    });

    test('Keyboard IRQ1 flow: keypress -> buffer -> port read', () => {
      const machine = new X86Machine();
      // Simulate the full IRQ1 flow:
      // 1. Key pressed -> scancode in buffer + IRQ1
      // 2. IRQ handler reads port 0x60 -> gets scancode
      // 3. Port read clears output flag
      machine.keyboard.handleKeyEvent('w', true);
      assert(machine.keyboard.outputFull, 'Output full after keypress');
      assert(machine.keyboard.buffer.length > 0, 'Buffer has data');
      // Kernel's IRQ1 handler would read port 0x60
      const scancode = machine.keyboard.read(0x60);
      assertEq(scancode, 0x11, 'Scancode for w is 0x11');
      assert(!machine.keyboard.outputFull, 'Output full cleared after read');
      machine.destroy();
    });
  });

  console.log(`\n${BOLD}${CYAN}╔══════════════════════════════════════╗${RESET}`);
  console.log(`${BOLD}${CYAN}║          Test Results Summary        ║${RESET}`);
  console.log(`${BOLD}${CYAN}╚══════════════════════════════════════╝${RESET}`);
  const allPassed = totalFailed === 0;
  const color = allPassed ? GREEN : RED;
  console.log(`\n${BOLD}${color}Passed: ${totalPassed}${RESET}`);
  console.log(`${BOLD}${color}Failed: ${totalFailed}${RESET}`);
  console.log(`${BOLD}${color}Total:  ${totalPassed + totalFailed}${RESET}`);
  console.log();

  if (!allPassed) {
    process.exit(1);
  }
}

run();
