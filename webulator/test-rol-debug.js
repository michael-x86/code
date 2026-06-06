const fs = require('fs');
const vm = require('vm');

const context = vm.createContext({
    module: { exports: {} },
    console: console,
    Uint8Array: Uint8Array,
    Uint16Array: Uint16Array,
    Uint32Array: Uint32Array,
    ArrayBuffer: ArrayBuffer
});

vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);
const X86Memory = context.module.exports.X86Memory;

vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);
const X86CPU = context.X86CPU || context.module.exports;

const mem = new X86Memory(4);
const cpu = new X86CPU(mem, null);

cpu.debug = true;

cpu.regs.eax = 0x80000001;
cpu.regs.eip = 0x2000;

mem.write8(0x2000, 0xD1);
mem.write8(0x2001, 0xE0);
cpu.regs.eip = 0x2000;

console.log('Before: EAX =', '0x' + (cpu.regs.eax >>> 0).toString(16));
cpu.step();
console.log('After:  EAX =', '0x' + (cpu.regs.eax >>> 0).toString(16));
console.log('Expected: EAX = 0x3');
