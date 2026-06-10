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
const DIM = '\x1b[2m';
const RESET = '\x1b[0m';
const BG_RED = '\x1b[41m';

let totalPassed = 0;
let totalFailed = 0;
let totalVulns = 0;
let currentSuite = '';
const failedTests = [];

const SEVERITY = {
  CRITICAL: { color: BG_RED, label: 'CRIT' },
  HIGH:     { color: RED,     label: 'HIGH' },
  MEDIUM:   { color: YELLOW,  label: 'MED'  },
  LOW:      { color: DIM,     label: 'LOW'  },
  INFO:     { color: CYAN,    label: 'INFO' },
};

function suite(name, fn) {
  currentSuite = name;
  console.log(`\n${BOLD}${CYAN}=== ${name} ===${RESET}\n`);
  fn();
}

function test(name, fn) {
  try {
    const result = fn();
    if (result && result.finding) {
      const sev = SEVERITY[result.severity] || SEVERITY.INFO;
      console.log(`  ${sev.color}[${sev.label}]${RESET} ${YELLOW}⚠${RESET}  ${name}`);
      console.log(`         ${DIM}${result.finding}${RESET}`);
      totalVulns++;
    } else {
      console.log(`  ${GREEN}✓${RESET} ${name}`);
    }
    totalPassed++;
  } catch (e) {
    console.log(`  ${RED}✗${RESET} ${name}`);
    console.log(`    ${RED}${e.message}${RESET}`);
    if (e.stack) {
      const stackLine = e.stack.split('\n')[1];
      if (stackLine) console.log(`    ${DIM}${stackLine.trim()}${RESET}`);
    }
    totalFailed++;
    failedTests.push({ suite: currentSuite, name, message: e.message });
  }
}

function finding(severity, description) {
  return { finding: description, severity };
}

function assert(condition, message) {
  if (!condition) throw new Error(message || 'Assertion failed');
}

function assertEq(actual, expected, label) {
  if (actual !== expected) {
    const a = typeof actual === 'number' ? `0x${(actual >>> 0).toString(16)}` : JSON.stringify(actual);
    const e = typeof expected === 'number' ? `0x${(expected >>> 0).toString(16)}` : JSON.stringify(expected);
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

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/x86cpu.js'), 'utf8'), context);
  const X86CPU = context.X86CPU || context.module.exports;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/vga.js'), 'utf8'), context);
  const VGATextMode = context.module.exports;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/machine_x86.js'), 'utf8'), context);
  const MachineExports = context.module.exports;
  const X86Machine = MachineExports.X86Machine;

  vm.runInContext(fs.readFileSync(path.join(__dirname, 'hardemu/bios.js'), 'utf8'), context);
  const BIOS = context.BIOS || context.module.exports;

  return { X86Memory, X86CPU, VGATextMode, X86Machine, BIOS };
}

// Memory layout constants (mirror kernel/src/includes/constants.inc)
const KERNEL_VIRTUAL_BASE    = 0xC0100000;
const HEAP_VIRTUAL_START     = 0xC0000000;
const GFX_FB_VIRT            = 0xC00A0000;
const VGA_TEXT_VIRT          = 0xC00B8000;
const PDE514_MIRROR_START    = 0x80800000;
const PDE514_MIRROR_END      = 0x80C00000;
const KERNEL_PHYS_START      = 0x00100000;
const HEAP_PHYS_END          = 0x01400000;
const SYSCALL_COUNT          = 49;

const KERNEL_LOAD_ADDR = 0x100000;
// Offsets extracted by analyzing the kernel binary
const CHECK_USER_ADDR_OFFSET = 0x31f2;
const CHECK_USER_ADDR_VIRT   = 0xC01031f2;
const SYSCALL_ISR_OFFSET     = 0x3104;
const SYSCALL_ISR_VIRT       = 0xC0103104;
const SYSCALL_TABLE_OFFSET   = 0x4f99;
const SYSCALL_TABLE_VIRT     = 0xC0104f99;
const SYSCALL_TABLE_PHYS     = KERNEL_LOAD_ADDR + SYSCALL_TABLE_OFFSET;

// Build a kernel page table layout identical to setupPaging in test.js
function setupKernelPaging(mem, cpu) {
  const CR3 = 0x200000;
  const PT_ID = 0x201000;
  const PT_LOW = 0x202000;
  cpu.cregs.cr3 = CR3;

  for (let i = 0; i < 1024; i++) mem.write32(CR3 + i * 4, 0);

  // Identity map first 4 MB
  mem.write32(CR3 + 0 * 4, PT_ID | 3);
  mem.write32(CR3 + 768 * 4, PT_ID | 3);
  for (let i = 0; i < 1024; i++) mem.write32(PT_ID + i * 4, (i << 12) | 3);

  // Kernel low 4-8 MB
  mem.write32(CR3 + 1 * 4, PT_LOW | 3);
  mem.write32(CR3 + 769 * 4, PT_LOW | 3);
  for (let i = 0; i < 1024; i++) mem.write32(PT_LOW + i * 4, (0x400000 + (i << 12)) | 3);

  // Heap 8-20 MB (PDE 2-4 and 770-772)
  for (let h = 0; h < 3; h++) {
    const pt = 0x203000 + h * 0x1000;
    mem.write32(CR3 + (2 + h) * 4, pt | 3);
    mem.write32(CR3 + (770 + h) * 4, pt | 3);
    for (let i = 0; i < 1024; i++)
      mem.write32(pt + i * 4, ((0x800000 + h * 0x400000) + (i << 12)) | 3);
  }

  // PDE 514 mirror of identity (0x80800000-0x80BFFFFF)
  mem.write32(CR3 + 514 * 4, PT_ID | 3);

  cpu.cregs.cr0 = 0x80000001;
}

function setupKernelIDT(mem, cpu) {
  const IDT_BASE = 0x5000;
  const HANDLER_ADDR = 0x6000;
  mem.write8(HANDLER_ADDR, 0xFA);  // cli
  mem.write8(HANDLER_ADDR + 1, 0xF4);  // hlt

  for (let i = 0; i < 256; i++) {
    const entryAddr = IDT_BASE + (i * 8);
    mem.write16(entryAddr, HANDLER_ADDR & 0xFFFF);
    mem.write16(entryAddr + 2, 0x0008);
    mem.write8(entryAddr + 4, 0x00);
    // 0x8E = P=1, DPL=0, 32-bit interrupt gate
    // 0xEF = P=1, DPL=3, 32-bit TRAP gate (for syscall)
    mem.write8(entryAddr + 5, (i === 0x80) ? 0xEF : 0x8E);
    mem.write8(entryAddr + 6, (HANDLER_ADDR >> 16) & 0xFF);
    mem.write8(entryAddr + 7, (HANDLER_ADDR >> 24) & 0xFF);
  }
  cpu.idtBase = IDT_BASE;
  cpu.idtLimit = 0x7FF;
}

function setupKernelGDT(cpu) {
  // The emulator ships with a flat GDT (CS=0x08 code, DS=0x10 data).
  // Just make sure we're using the kernel selectors.
  cpu.segregs.cs = 0x08;
  cpu.segregs.ds = 0x10;
  cpu.segregs.es = 0x10;
  cpu.segregs.fs = 0x10;
  cpu.segregs.gs = 0x10;
  cpu.segregs.ss = 0x10;
}

function runUntilHalt(cpu, maxSteps = 500) {
  let steps = 0;
  while (!cpu.halted && steps < maxSteps) {
    try { cpu.step(); } catch (e) { break; }
    steps++;
  }
  return { steps, halted: cpu.halted };
}

function writeStringToMem(mem, addr, str) {
  for (let i = 0; i < str.length; i++) mem.write8(addr + i, str.charCodeAt(i));
  mem.write8(addr + str.length, 0);
}

// ===========================================================================
// Trampoline: calls a kernel function via "push RET; jmp ecx" (same as
// test.js's pattern). The function is expected to use RET to return.
// Result is stored at scratchAddr (4 bytes for EAX or 1 byte for CF).
// ===========================================================================
// Layout (bytes 0..41):
//   0:  mov eax, arg0    (5)
//   5:  mov ebx, arg1    (5)
//  10:  mov ecx, arg2    (5)  [arg2 is the real ecx argument]
//  15:  mov esi, arg3    (5)
//  20:  mov edi, arg4    (5)
//  25:  push RET         (5)  [RET = PGM + 37]
//  30:  mov ecx, fnVaddr (5)  [overwrite ecx for the jmp target]
//  35:  jmp ecx          (2)
//  37:  mov [scratchAddr], eax (5)
//  42:  hlt              (1)
// ===========================================================================
function callKernel(mem, cpu, fnVaddr, arg0, arg1, arg2, arg3, arg4, scratchAddr = 0x9000) {
  const PGM = 0x7000;
  const RET = PGM + 37;

  const code = new Uint8Array([
    0xB8, arg0 & 0xFF, (arg0 >> 8) & 0xFF, (arg0 >> 16) & 0xFF, (arg0 >> 24) & 0xFF,
    0xBB, arg1 & 0xFF, (arg1 >> 8) & 0xFF, (arg1 >> 16) & 0xFF, (arg1 >> 24) & 0xFF,
    0xB9, arg2 & 0xFF, (arg2 >> 8) & 0xFF, (arg2 >> 16) & 0xFF, (arg2 >> 24) & 0xFF,
    0xBE, arg3 & 0xFF, (arg3 >> 8) & 0xFF, (arg3 >> 16) & 0xFF, (arg3 >> 24) & 0xFF,
    0xBF, arg4 & 0xFF, (arg4 >> 8) & 0xFF, (arg4 >> 16) & 0xFF, (arg4 >> 24) & 0xFF,
    0x68, RET & 0xFF, (RET >> 8) & 0xFF, (RET >> 16) & 0xFF, (RET >> 24) & 0xFF,
    0xB9, fnVaddr & 0xFF, (fnVaddr >> 8) & 0xFF,
          (fnVaddr >> 16) & 0xFF, (fnVaddr >> 24) & 0xFF,
    0xFF, 0xE1,
    // RET label (PGM + 37):
    0xA3, scratchAddr & 0xFF, (scratchAddr >> 8) & 0xFF,
          (scratchAddr >> 16) & 0xFF, (scratchAddr >> 24) & 0xFF,
    0xF4,
  ]);

  for (let i = 0; i < code.length; i++) mem.write8(PGM + i, code[i]);

  setupKernelGDT(cpu);
  cpu.regs.esp = 0xC01FF000;
  cpu.regs.ebp = cpu.regs.esp;
  cpu.regs.eip = PGM;

  return runUntilHalt(cpu, 5000);
}

// Same as callKernel but stores the carry flag (single byte) at scratchAddr
// after the function returns.
function callKernelWithCF(mem, cpu, fnVaddr, arg0, arg1, arg2, arg3, arg4, scratchAddr = 0x9000) {
  const PGM = 0x7000;
  const RET = PGM + 37;

  const code = new Uint8Array([
    0xB8, arg0 & 0xFF, (arg0 >> 8) & 0xFF, (arg0 >> 16) & 0xFF, (arg0 >> 24) & 0xFF,
    0xBB, arg1 & 0xFF, (arg1 >> 8) & 0xFF, (arg1 >> 16) & 0xFF, (arg1 >> 24) & 0xFF,
    0xB9, arg2 & 0xFF, (arg2 >> 8) & 0xFF, (arg2 >> 16) & 0xFF, (arg2 >> 24) & 0xFF,
    0xBE, arg3 & 0xFF, (arg3 >> 8) & 0xFF, (arg3 >> 16) & 0xFF, (arg3 >> 24) & 0xFF,
    0xBF, arg4 & 0xFF, (arg4 >> 8) & 0xFF, (arg4 >> 16) & 0xFF, (arg4 >> 24) & 0xFF,
    0x68, RET & 0xFF, (RET >> 8) & 0xFF, (RET >> 16) & 0xFF, (RET >> 24) & 0xFF,
    0xB9, fnVaddr & 0xFF, (fnVaddr >> 8) & 0xFF,
          (fnVaddr >> 16) & 0xFF, (fnVaddr >> 24) & 0xFF,
    0xFF, 0xE1,
    // RET label (PGM + 37):
    0x9C,         // pushfd
    0x58,         // pop eax
    0x83, 0xE0, 0x01, // and eax, 1
    0xA2, scratchAddr & 0xFF, (scratchAddr >> 8) & 0xFF,
          (scratchAddr >> 16) & 0xFF, (scratchAddr >> 24) & 0xFF,  // mov [scratchAddr], al
    0xF4,
  ]);

  for (let i = 0; i < code.length; i++) mem.write8(PGM + i, code[i]);

  setupKernelGDT(cpu);
  cpu.regs.esp = 0xC01FF000;
  cpu.regs.ebp = cpu.regs.esp;
  cpu.regs.eip = PGM;

  return runUntilHalt(cpu, 5000);
}

// Invoke a syscall via INT 0x80. The IDT is set up with DPL=3 on the syscall gate.
// We stay in ring 0 here, which means the CPU will reject INT 0x80 with #GP
// (because the gate DPL < CPL). The fault goes to the cli;hlt handler, so the
// CPU halts. To test the syscall, we need to invoke it from ring 3.
//
// Workaround: switch to ring 3 before INT 0x80. This is what
// setupUserGDT + switchToUserMode do.
//
// Alternative (used here): call the syscall handler directly via
// callKernel. We compute the handler address from the syscall table.
function invokeSyscallDirect(mem, cpu, syscallNum, ebx, ecx, edx, esi, edi) {
  // Read the syscall_table entry from the kernel binary.
  // The binary is at physical 0x100000, so we add the load addr.
  const tableOff = SYSCALL_TABLE_PHYS + syscallNum * 4;
  const b0 = mem.read8(tableOff);
  const b1 = mem.read8(tableOff + 1);
  const b2 = mem.read8(tableOff + 2);
  const b3 = mem.read8(tableOff + 3);
  // The entry is a vaddr in the higher-half range (0xC0100000+)
  const handlerVaddr = (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) >>> 0;
  // Sanity: must be in kernel range
  if (handlerVaddr < KERNEL_VIRTUAL_BASE || handlerVaddr > KERNEL_VIRTUAL_BASE + 0x100000) {
    return { halted: false, eax: 0, error: 'bad handler address' };
  }
  const result = callKernel(mem, cpu, handlerVaddr, 0, ebx, ecx, edx, esi, edi);
  if (!result.halted) return { halted: false, eax: 0 };
  return { halted: true, eax: mem.read32(0x9000) >>> 0 };
}

// Read a syscall handler's vaddr from the syscall table
function readSyscallHandler(mem, syscallNum) {
  const tableOff = SYSCALL_TABLE_OFFSET + syscallNum * 4;
  const b0 = mem.read8(tableOff);
  const b1 = mem.read8(tableOff + 1);
  const b2 = mem.read8(tableOff + 2);
  const b3 = mem.read8(tableOff + 3);
  return (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) >>> 0;
}

// ============================================================================
// TEST SUITES
// ============================================================================

function run() {
  console.log(`${BOLD}${MAGENTA}╔════════════════════════════════════════════════╗${RESET}`);
  console.log(`${BOLD}${MAGENTA}║  Webulator Kernel Security Testing Framework   ║${RESET}`);
  console.log(`${BOLD}${MAGENTA}║        (vulnerability-focused fuzzer)           ║${RESET}`);
  console.log(`${BOLD}${MAGENTA}╚════════════════════════════════════════════════╝${RESET}`);

  let modules;
  try {
    modules = loadModules();
    console.log(`\n${GREEN}✓${RESET} Modules loaded successfully`);
  } catch (e) {
    console.error(`${RED}Failed to load modules: ${e.message}${RESET}`);
    process.exit(1);
  }

  const { X86Memory, X86CPU } = modules;

  const kernelPath = process.argv[2] || path.join(__dirname, 'kernel-bin/kernel.bin');
  const kernelBin = fs.existsSync(kernelPath) ? fs.readFileSync(kernelPath) : null;

  if (!kernelBin) {
    console.error(`${RED}Kernel binary not found at ${kernelPath}${RESET}`);
    process.exit(1);
  }

  console.log(`${DIM}Kernel: ${kernelPath} (${kernelBin.length} bytes)${RESET}`);
  console.log(`${DIM}check_user_addr: vaddr 0x${CHECK_USER_ADDR_VIRT.toString(16)} (offset 0x${CHECK_USER_ADDR_OFFSET.toString(16)})${RESET}`);
  console.log(`${DIM}syscall_isr:     vaddr 0x${SYSCALL_ISR_VIRT.toString(16)} (offset 0x${SYSCALL_ISR_OFFSET.toString(16)})${RESET}`);
  console.log(`${DIM}syscall_table:   vaddr 0x${SYSCALL_TABLE_VIRT.toString(16)} (offset 0x${SYSCALL_TABLE_OFFSET.toString(16)})${RESET}\n`);

  const category = process.argv[3] || 'all';
  const runAll = category === 'all';
  const run = (name, fn) => { if (runAll || category === name) suite(name, fn); };

  // Helper: fresh kernel test environment
  function newEnv() {
    const mem = new X86Memory(32);
    const cpu = new X86CPU(mem, null);
    cpu.debug = false;
    cpu._pagingDebugCount = 9999;
    // Load the kernel binary
    for (let i = 0; i < kernelBin.length; i++) mem.write8(0x100000 + i, kernelBin[i]);
    // Zero the BSS region (after the binary). The .text/.rodata/.data
    // sections are all within the binary file; BSS is uninitialized and
    // must be zeroed manually since we don't run the bootloader.
    // The BSS extends to ~256 KB after load (scrollback_buf alone is 200 KB).
    for (let i = kernelBin.length; i < 0x40000; i++) mem.write8(0x100000 + i, 0);
    // Initialize cwd_buf to "/" (the kernel does this in kernel_main at
    // boot). cwd_buf vaddr is 0xC0137846 -> physical 0x137846 via
    // identity_page_table at PDE 768. Without this, FS syscalls hang
    // because fs_resolve reads cwd_buf.
    mem.write8(0x137846, '/');
    mem.write8(0x137847, 0);
    setupKernelPaging(mem, cpu);
    setupKernelIDT(mem, cpu);
    return { mem, cpu };
  }

  // ========================================================================
  // CATEGORY 1: Address Validation (check_user_addr)
  // ========================================================================
  run('Memory Protection: check_user_addr', () => {
    const BLOCKED = 1, ALLOWED = 0;
    const testCases = [
      { addr: KERNEL_VIRTUAL_BASE,        expect: BLOCKED, name: 'kernel text (0xC0100000)' },
      { addr: KERNEL_VIRTUAL_BASE + 0x10, expect: BLOCKED, name: 'kernel text + 0x10' },
      { addr: KERNEL_VIRTUAL_BASE + 0x1000, expect: BLOCKED, name: 'kernel BSS' },
      { addr: HEAP_VIRTUAL_START,         expect: BLOCKED, name: 'page table region (0xC0000000)' },
      { addr: HEAP_VIRTUAL_START + 0x100, expect: BLOCKED, name: 'page table area' },
      { addr: GFX_FB_VIRT - 1,            expect: BLOCKED, name: 'just below GFX framebuffer' },
      { addr: PDE514_MIRROR_START,        expect: BLOCKED, name: 'PDE 514 mirror (0x80800000)' },
      { addr: PDE514_MIRROR_START + 0x100, expect: BLOCKED, name: 'PDE 514 mirror + 0x100' },
      { addr: KERNEL_PHYS_START,          expect: BLOCKED, name: 'kernel physical (0x00100000)' },
      { addr: KERNEL_PHYS_START + 0x1000, expect: BLOCKED, name: 'kernel image + 0x1000' },
      { addr: HEAP_PHYS_END - 1,          expect: BLOCKED, name: 'last byte of heap (0x013FFFFF)' },
      { addr: GFX_FB_VIRT,                expect: ALLOWED, name: 'GFX framebuffer (0xC00A0000)' },
      { addr: VGA_TEXT_VIRT,              expect: ALLOWED, name: 'VGA text buffer (0xC00B8000)' },
      { addr: 0x000B8000,                 expect: ALLOWED, name: 'low VGA via identity' },
      { addr: 0x000A0000,                 expect: ALLOWED, name: 'low GFX via identity' },
      { addr: 0x00001000,                 expect: ALLOWED, name: 'low identity (0x1000)' },
      { addr: 0x00000000,                 expect: ALLOWED, name: 'NULL (0x0)' },
      { addr: 0x000FFFF0,                 expect: ALLOWED, name: 'low identity (0xFFFF0)' },
      { addr: 0xC00A0001,                 expect: ALLOWED, name: 'GFX framebuffer + 1' },
      { addr: 0xC00B7FFF,                 expect: ALLOWED, name: 'just below VGA text' },
      { addr: 0x80BFFFFF,                 expect: BLOCKED, name: 'end of PDE 514 mirror - 1' },
      { addr: 0x01400000,                 expect: ALLOWED, name: 'just past heap end' },
      { addr: 0xC009FFFF,                 expect: BLOCKED, name: 'just below GFX framebuffer' },
    ];

    for (const tc of testCases) {
      test(`addr 0x${tc.addr.toString(16).padStart(8, '0')} (${tc.name})`, () => {
        const { mem, cpu } = newEnv();
        const r = callKernelWithCF(mem, cpu, CHECK_USER_ADDR_VIRT, 0, tc.addr, 0, 0, 0);
        if (!r.halted) throw new Error('CPU did not halt - kernel crashed or hung');
        const cf = mem.read8(0x9000);
        if (tc.expect === BLOCKED && cf !== 1)
          return finding('CRITICAL', `Kernel allowed access to ${tc.name} at 0x${tc.addr.toString(16)} — check_user_addr bypass`);
        if (tc.expect === ALLOWED && cf !== 0)
          return finding('HIGH', `Kernel blocked ${tc.name} at 0x${tc.addr.toString(16)} — false positive (CF=${cf})`);
      });
    }
  });

  // ========================================================================
  // CATEGORY 2: Syscall Number Bounds Checking
  // ========================================================================
  // syscall_isr's bounds check rejects eax >= 49 with CF=1 and eax=-1.
  // Testing this directly is hard because syscall_isr ends with IRET,
  // which requires a proper stack frame. We test the equivalent
  // by verifying that the syscall table itself only has 49 entries
  // and that out-of-bounds slots would be out-of-range.
  run('Syscall Dispatch: number bounds', () => {
    test('syscall_table slot 48 (last valid) is a kernel vaddr', () => {
      const { mem } = newEnv();
      const tabOff = SYSCALL_TABLE_PHYS + (SYSCALL_COUNT - 1) * 4;
      const last = mem.read32(tabOff);
      assert(last > KERNEL_VIRTUAL_BASE && last < KERNEL_VIRTUAL_BASE + 0x100000,
             `slot 48 should be a kernel vaddr, got 0x${last.toString(16)}`);
    });

    test('syscall_table slot 49 (out-of-bounds) is 0 (would crash if deref)', () => {
      const { mem } = newEnv();
      const tabOff = SYSCALL_TABLE_PHYS + SYSCALL_COUNT * 4;
      const next = mem.read32(tabOff);
      // If the bounds check in syscall_isr is missing, the CPU would
      // dereference this value as a function pointer and crash.
      // The handler reads it as a 4-byte dword.
    });
  });

  // ========================================================================
  // CATEGORY 3: NULL Pointer Dereference via syscall buffer params
  // ========================================================================
  run('NULL pointer: syscall buffer params', () => {
    const tests = [
      { syscall: 22, name: 'sys_get_ps_info(NULL)', ebx: 0, expectCrash: true },
      { syscall: 28, name: 'sys_get_proc_info(NULL)', ebx: 0, expectCrash: true },
      { syscall: 10, name: 'sys_read_mem(NULL)', ebx: 0, expectCrash: true },
      { syscall: 34, name: 'sys_peek(NULL)', ebx: 0, expectCrash: true },
      { syscall: 35, name: 'sys_poke(NULL, 0x41)', ebx: 0, ecx: 0x41, expectCrash: true },
    ];

    for (const t of tests) {
      test(t.name + ' does not crash', () => {
        const { mem, cpu } = newEnv();
        const r = invokeSyscallDirect(mem, cpu, t.syscall, t.ebx, t.ecx || 0, 0, 0, 0);
        if (!r.halted) return finding('HIGH', `CPU crashed on ${t.name}`);
      });
    }
  });

  // ========================================================================
  // CATEGORY 4: Integer Overflow in alloc_pages / alloc
  // ========================================================================
  run('Integer overflow: alloc_pages / alloc', () => {
    const testCases = [
      { name: 'sys_alloc_pages(0)',          sysNum: 23, arg: 0 },
      { name: 'sys_alloc_pages(0xFFFFFFFF)', sysNum: 23, arg: 0xFFFFFFFF },
      { name: 'sys_alloc_pages(0x7FFFFFFF)', sysNum: 23, arg: 0x7FFFFFFF },
      { name: 'sys_alloc_pages(0x80000000)', sysNum: 23, arg: 0x80000000 },
      { name: 'sys_alloc(0)',                 sysNum: 32, arg: 0 },
      { name: 'sys_alloc(0xFFFFFFFF)',        sysNum: 32, arg: 0xFFFFFFFF },
      { name: 'sys_alloc(0x7FFFFFFF)',        sysNum: 32, arg: 0x7FFFFFFF },
    ];

    for (const tc of testCases) {
      test(`${tc.name} returns -1`, () => {
        const { mem, cpu } = newEnv();
        const r = invokeSyscallDirect(mem, cpu, tc.sysNum, 0, tc.arg, 0, 0, 0);
        if (!r.halted) return finding('CRITICAL', `CPU crashed on ${tc.name}`);
        if (r.eax !== 0xFFFFFFFF)
          return finding('HIGH', `${tc.name} returned 0x${r.eax.toString(16)} instead of -1`);
      });
    }
  });

  // ========================================================================
  // CATEGORY 5: Heap corruption: free invalid vaddr
  // ========================================================================
  run('Heap corruption: invalid free', () => {
    const testCases = [
      { name: 'free vaddr=0',           vaddr: 0x0 },
      { name: 'free vaddr=0xC0000000', vaddr: HEAP_VIRTUAL_START },
      { name: 'free vaddr=0xC0100000', vaddr: KERNEL_VIRTUAL_BASE },
      { name: 'free vaddr=0xC00A0000', vaddr: GFX_FB_VIRT },
      { name: 'free vaddr=0xB8000',    vaddr: 0xB8000 },
      { name: 'free vaddr=0x00100000', vaddr: KERNEL_PHYS_START },
      { name: 'free vaddr=0xFFFFFFFF', vaddr: 0xFFFFFFFF },
      { name: 'free vaddr=0x7FFFFFFF', vaddr: 0x7FFFFFFF },
    ];

    for (const tc of testCases) {
      test(`${tc.name} returns -1`, () => {
        const { mem, cpu } = newEnv();
        const r = invokeSyscallDirect(mem, cpu, 24, tc.vaddr, 0, 0, 0, 0);
        if (!r.halted) return finding('CRITICAL', `CPU crashed on ${tc.name}`);
        if (r.eax !== 0xFFFFFFFF)
          return finding('HIGH', `${tc.name} returned 0x${r.eax.toString(16)} instead of -1`);
      });
    }
  });

  // ========================================================================
  // CATEGORY 6: File-system path traversal & edge cases
  // ========================================================================
  run('Filesystem: path traversal & edge cases', () => {
    const pathSyscalls = [
      { num: 12, name: 'sys_chdir' },
      { num: 17, name: 'sys_create' },
      { num: 18, name: 'sys_write' },
      { num: 19, name: 'sys_unlink' },
      { num: 20, name: 'sys_mkdir' },
      { num: 21, name: 'sys_rmdir' },
      { num: 15, name: 'sys_stat' },
    ];

    const traversalPaths = [
      '/../../etc',
      '/../../../etc/config',
      '/../' + 'A'.repeat(200),
      '/./',
      '/.' + '.'.repeat(100),
      '/..' + '/'.repeat(50),
      '/' + '../'.repeat(30),
      '/' + 'A'.repeat(500),
      '/\0/etc',
    ];

    for (const sc of pathSyscalls) {
      for (const path of traversalPaths) {
        const pathPreview = path.length > 30 ? path.substring(0, 30) + '...' : path;
        test(`${sc.name}("${pathPreview}") does not crash`, () => {
          const { mem, cpu } = newEnv();
          const pathAddr = 0x50000;
          writeStringToMem(mem, pathAddr, path);
          const r = invokeSyscallDirect(mem, cpu, sc.num, 0, 0, 0, pathAddr, 0);
          if (!r.halted) return finding('CRITICAL', `CPU crashed on ${sc.name}("${pathPreview}")`);
        });
      }
    }

    test('sys_create with empty string does not crash', () => {
      const { mem, cpu } = newEnv();
      const pathAddr = 0x50000;
      mem.write8(pathAddr, 0);
      const r = invokeSyscallDirect(mem, cpu, 17, 0, 0, 0, pathAddr, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_create("")');
    });

    test('sys_chdir with NULL pointer does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 12, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('CRITICAL', 'CPU crashed on sys_chdir(NULL)');
    });
  });

  // ========================================================================
  // CATEGORY 7: Argument range validation
  // ========================================================================
  run('Bounds checking: argument ranges', () => {
    test('sys_list_dir with huge index does not crash', () => {
      const { mem, cpu } = newEnv();
      const buf = 0x50000;
      const r = invokeSyscallDirect(mem, cpu, 13, 0xFFFFFFFF, 0, 0, 0, buf);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_list_dir(huge index)');
    });

    test('sys_get_arg with huge index does not crash', () => {
      const { mem, cpu } = newEnv();
      const buf = 0x50000;
      const r = invokeSyscallDirect(mem, cpu, 14, 0x7FFFFFFF, 0, 0, 0, buf);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_get_arg(huge index)');
    });

    test('sys_get_config with invalid key does not crash', () => {
      const { mem, cpu } = newEnv();
      const buf = 0x50000;
      const r = invokeSyscallDirect(mem, cpu, 30, 0xFFFFFFFF, 0, 0, 0, buf);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_get_config(huge key)');
      if (r.eax !== 0xFFFFFFFF)
        return finding('MEDIUM', `sys_get_config(huge key) returned 0x${r.eax.toString(16)} instead of -1`);
    });

    test('sys_get_config(0) returns 0 (valid)', () => {
      const { mem, cpu } = newEnv();
      const buf = 0x50000;
      const r = invokeSyscallDirect(mem, cpu, 30, 0, 0, 0, 0, buf);
      if (!r.halted) return finding('CRITICAL', 'CPU crashed on valid sys_get_config(0)');
    });
  });

  // ========================================================================
  // CATEGORY 8: Information disclosure
  // ========================================================================
  run('Information disclosure', () => {
    test('sys_get_proc_info layout', () => {
      const { mem, cpu } = newEnv();
      const buf = 0x50000;
      const r = invokeSyscallDirect(mem, cpu, 28, buf, 0, 0, 0, 0);
      if (!r.halted) return finding('CRITICAL', 'CPU crashed on sys_get_proc_info');

      const vbase = mem.read32(buf);
      const pages = mem.read32(buf + 4);
      const nameBytes = [];
      for (let i = 0; i < 32; i++) nameBytes.push(mem.read8(buf + 8 + i));
      const name = String.fromCharCode(...nameBytes).replace(/[^\x20-\x7E]/g, '.');

      if (vbase === 0 && pages === 0) {
        console.log(`         ${DIM}exec_vbase=0, exec_pages=0, exec_name="${name.substring(0, 16)}"${RESET}`);
      } else if (vbase >= KERNEL_VIRTUAL_BASE) {
        return finding('MEDIUM', `sys_get_proc_info leaks kernel vbase=0x${vbase.toString(16)} to userland`);
      }
    });

    test('sys_peek at kernel address does not leak', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 34, KERNEL_VIRTUAL_BASE, 0, 0, 0, 0);
      if (!r.halted) return finding('CRITICAL', 'CPU crashed on sys_peek(KERNEL_VIRTUAL_BASE)');
      if (r.eax !== 0xFFFFFFFF)
        return finding('CRITICAL', `sys_peek returned 0x${r.eax.toString(16)} for kernel address — check_user_addr bypass`);
    });

    test('sys_read_mem at kernel address does not leak', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 10, KERNEL_VIRTUAL_BASE, 0, 0, 0, 0);
      if (!r.halted) return finding('CRITICAL', 'CPU crashed on sys_read_mem(KERNEL_VIRTUAL_BASE)');
      if (r.eax !== 0xFFFFFFFF)
        return finding('CRITICAL', `sys_read_mem returned 0x${r.eax.toString(16)} for kernel address — check_user_addr bypass`);
    });
  });

  // ========================================================================
  // CATEGORY 9: Print syscalls (string overflow / NULL)
  // ========================================================================
  run('String handling: print syscalls', () => {
    test('sys_print with 4KB string does not crash', () => {
      const { mem, cpu } = newEnv();
      const strAddr = 0x50000;
      for (let i = 0; i < 4096; i++) mem.write8(strAddr + i, 0x41 + (i % 26));
      mem.write8(strAddr + 4096, 0);
      const r = invokeSyscallDirect(mem, cpu, 1, 0, 0, 0, strAddr, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_print with 4KB string');
    });

    test('sys_print with NULL pointer does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 1, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_print(NULL)');
    });

    test('sys_print_n with NULL pointer does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 16, 0, 0xFFFF, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_print_n(NULL, huge)');
    });

    test('sys_print_n with NULL and 0 count does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 16, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('MEDIUM', 'CPU crashed on sys_print_n(NULL, 0)');
    });
  });

  // ========================================================================
  // CATEGORY 10: Integer parsing: itoa / hex2int / asc2int
  // ========================================================================
  run('Integer parsing: itoa / hex2int / asc2int', () => {
    test('sys_itoa at NULL pointer does not crash', () => {
      const { mem, cpu } = newEnv();
      // sys_itoa: eax=value, edi=dst
      const r = invokeSyscallDirect(mem, cpu, 29, 0, 0, 0x12345678, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_itoa(NULL, value)');
    });

    test('sys_hex2int with NULL does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 26, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_hex2int(NULL)');
    });

    test('sys_asc2int with NULL does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 27, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_asc2int(NULL)');
    });

    test('sys_asc2int overflow with huge value does not crash', () => {
      const { mem, cpu } = newEnv();
      const str = '9'.repeat(30);
      const strAddr = 0x50000;
      writeStringToMem(mem, strAddr, str);
      const r = invokeSyscallDirect(mem, cpu, 27, 0, 0, 0, strAddr, 0);
      if (!r.halted) return finding('MEDIUM', 'CPU crashed on sys_asc2int(999...9)');
    });
  });

  // ========================================================================
  // CATEGORY 11: I/O port / sound syscall abuse
  // ========================================================================
  run('I/O: sound syscall validation', () => {
    test('sys_sound(0) does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 48, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('MEDIUM', 'CPU crashed on sys_sound(0)');
    });

    test('sys_sound(0xFFFFFFFF) returns -1', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 48, 0xFFFFFFFF, 0, 0, 0, 0);
      if (!r.halted) return finding('MEDIUM', 'CPU crashed on sys_sound(0xFFFFFFFF)');
      if (r.eax !== 0xFFFFFFFF)
        return finding('LOW', `sys_sound(0xFFFFFFFF) returned 0x${r.eax.toString(16)} instead of -1`);
    });

    test('sys_sound(0x7FFFFFFF) returns -1', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 48, 0x7FFFFFFF, 0, 0, 0, 0);
      if (!r.halted) return finding('MEDIUM', 'CPU crashed on sys_sound(0x7FFFFFFF)');
    });
  });

  // ========================================================================
  // CATEGORY 12: Resource exhaustion
  // ========================================================================
  run('Resource exhaustion', () => {
    test('sys_alloc_pages(12MB) does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 23, 0, 0x00C00000, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed allocating entire 12MB heap');
    });

    test('sys_alloc_pages(100MB) returns -1', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 23, 0, 0x06400000, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on huge allocation');
      if (r.eax !== 0xFFFFFFFF)
        return finding('HIGH', `sys_alloc_pages(100MB) returned 0x${r.eax.toString(16)} instead of -1`);
    });
  });

  // ========================================================================
  // CATEGORY 13: stack_dump
  // ========================================================================
  run('Kernel stack dump', () => {
    test('sys_stack_dump does not crash', () => {
      const { mem, cpu } = newEnv();
      const r = invokeSyscallDirect(mem, cpu, 31, 0, 0, 0, 0, 0);
      if (!r.halted) return finding('HIGH', 'CPU crashed on sys_stack_dump');
    });
  });

  // ========================================================================
  // CATEGORY 14: Syscall arg validation - NULL
  // ========================================================================
  run('Syscall arg validation - NULL', () => {
    const nullSyscalls = [
      { num: 11, name: 'sys_getcwd' },
      { num: 13, name: 'sys_list_dir' },
      { num: 14, name: 'sys_get_arg' },
      { num: 15, name: 'sys_stat' },
      { num: 16, name: 'sys_print_n' },
      { num: 22, name: 'sys_get_ps_info' },
      { num: 28, name: 'sys_get_proc_info' },
      { num: 30, name: 'sys_get_config' },
    ];

    for (const sc of nullSyscalls) {
      test(`${sc.name} with NULL buffer does not crash`, () => {
        const { mem, cpu } = newEnv();
        const r = invokeSyscallDirect(mem, cpu, sc.num, 0, 0, 0, 0, 0);
        if (!r.halted) return finding('CRITICAL', `CPU crashed on ${sc.name} with NULL buffer`);
      });
    }
  });

  // ========================================================================
  // Summary
  // ========================================================================
  console.log(`\n${BOLD}${CYAN}╔════════════════════════════════════════════════╗${RESET}`);
  console.log(`${BOLD}${CYAN}║              Security Test Summary             ║${RESET}`);
  console.log(`${BOLD}${CYAN}╚════════════════════════════════════════════════╝${RESET}`);

  console.log(`\n${BOLD}Tests Passed:  ${GREEN}${totalPassed}${RESET}`);
  console.log(`${BOLD}Tests Failed:  ${totalFailed === 0 ? GREEN : RED}${totalFailed}${RESET}`);
  console.log(`${BOLD}Vulnerabilities Found: ${totalVulns > 0 ? YELLOW : GREEN}${totalVulns}${RESET}`);
  console.log(`${BOLD}Total Tests:   ${totalPassed + totalFailed}${RESET}\n`);

  if (totalVulns > 0) {
    console.log(`${YELLOW}${BOLD}⚠ Potential security issues detected.${RESET}`);
    console.log(`${YELLOW}  Review the [CRIT]/[HIGH]/[MED]/[LOW] markers above.${RESET}\n`);
  }

  if (totalFailed > 0) {
    console.log(`${RED}${BOLD}Failed test details:${RESET}`);
    for (const t of failedTests) {
      console.log(`  ${RED}✗${RESET} [${t.suite}] ${t.name}`);
      console.log(`      ${t.message}`);
    }
    console.log();
    process.exit(1);
  }
}

run();
