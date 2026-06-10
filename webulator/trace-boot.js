// Trace harness: boot kernel + disk like the app does, run until halt/fault,
// and dump the last N executed instructions with decoded operands.
const fs = require('fs');
const vm = require('vm');
const path = require('path');

const context = vm.createContext({
  module: { exports: {} }, console, Uint8Array, Uint16Array, Uint32Array,
  Int8Array, Int16Array, Int32Array, ArrayBuffer, DataView, Math, performance: { now: () => 0 },
});
function load(f) { vm.runInContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), context); }
load('hardemu/memory.js');
const X86Memory = context.module.exports.X86Memory;
load('hardemu/x86cpu.js');
const X86CPU = context.X86CPU || context.module.exports;
load('hardemu/vga.js');
load('hardemu/machine_x86.js');
const X86Machine = context.module.exports.X86Machine;
load('hardemu/bios.js');

const machine = new X86Machine();
machine.setupInitialIDT();

// Load disk image (os.img = bootloader + kernel + fs)
const osimg = fs.readFileSync(path.join(__dirname, 'kernel-bin/os.img'));
machine.loadDiskImage(osimg.buffer.slice(osimg.byteOffset, osimg.byteOffset + osimg.byteLength));
machine.mem.loadDiskImage(osimg.buffer.slice(osimg.byteOffset, osimg.byteOffset + osimg.byteLength));

// Load kernel at 0x100000 directly (app does this, skipping bootloader)
const kbin = fs.readFileSync(path.join(__dirname, 'kernel-bin/kernel.bin'));
machine.mem.loadBinary(kbin, 0x100000);
machine.cpu.regs.eip = 0x100000;

const cpu = machine.cpu;

// Ring buffer of recent instructions
const RING = 60;
const ring = [];
let count = 0;

// Hook readMem to capture data accesses (vaddr) per instruction
const origRead = cpu.readMem.bind(cpu);
let lastDataAddr = null;
cpu.readMem = function (addr, size) {
  // Only record data reads (not instruction fetches at eip)
  if (addr !== this.regs.eip) lastDataAddr = addr >>> 0;
  return origRead(addr, size);
};
const origWrite = cpu.writeMem.bind(cpu);
cpu.writeMem = function (addr, value, size) {
  lastDataAddr = addr >>> 0;
  return origWrite(addr, value, size);
};

// Silence the very chatty paging logger and stop on first #PF
const origTrigger = cpu.triggerException.bind(cpu);
let faulted = null;
cpu.triggerException = function (vec, err) {
  if (faulted === null) {
    faulted = { vec, err, eip: this.regs.eip >>> 0, cr2: this.cregs.cr2 >>> 0 };
  }
  this.halted = true; // stop the run immediately, avoid recursion
};

const MAX = 5_000_000;
while (!cpu.halted && count < MAX) {
  const eip = cpu.regs.eip >>> 0;
  const op0 = origRead(eip, 1);
  const op1 = origRead(eip + 1, 1);
  const op2 = origRead(eip + 2, 1);
  const op3 = origRead(eip + 3, 1);
  lastDataAddr = null;
  const regsBefore = {
    eax: cpu.regs.eax >>> 0, ecx: cpu.regs.ecx >>> 0, edx: cpu.regs.edx >>> 0,
    ebx: cpu.regs.ebx >>> 0, esp: cpu.regs.esp >>> 0, ebp: cpu.regs.ebp >>> 0,
    esi: cpu.regs.esi >>> 0, edi: cpu.regs.edi >>> 0,
  };
  try {
    cpu.step();
  } catch (e) {
    console.log('EXCEPTION at step:', e.message);
    break;
  }
  ring.push({
    n: count, eip, bytes: [op0, op1, op2, op3],
    dataAddr: lastDataAddr, regs: regsBefore,
    eipAfter: cpu.regs.eip >>> 0,
  });
  if (ring.length > RING) ring.shift();
  count++;
}

console.log(`\nStopped after ${count} instructions. halted=${cpu.halted}`);
if (faulted) console.log(`FAULT vec=${faulted.vec} err=${faulted.err} eip=0x${faulted.eip.toString(16)} cr2=0x${faulted.cr2.toString(16)}`);
console.log(`Final EIP=0x${(cpu.regs.eip >>> 0).toString(16)} CR2=0x${(cpu.cregs.cr2 >>> 0).toString(16)}\n`);
console.log('=== Last instructions ===');
for (const r of ring) {
  const b = r.bytes.map((x) => x.toString(16).padStart(2, '0')).join(' ');
  const da = r.dataAddr !== null ? ` DATA=0x${r.dataAddr.toString(16)}` : '';
  console.log(
    `[${r.n}] EIP=0x${r.eip.toString(16)} [${b}] -> 0x${r.eipAfter.toString(16)}${da}` +
    ` | EAX=${r.regs.eax.toString(16)} ECX=${r.regs.ecx.toString(16)} EDX=${r.regs.edx.toString(16)}` +
    ` EBX=${r.regs.ebx.toString(16)} ESI=${r.regs.esi.toString(16)} EDI=${r.regs.edi.toString(16)} EBP=${r.regs.ebp.toString(16)} ESP=${r.regs.esp.toString(16)}`
  );
}
