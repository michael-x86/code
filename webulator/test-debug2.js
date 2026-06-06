const fs = require('fs');
const vm = require('vm');
const context = vm.createContext({module: {exports: {}}, console, Uint8Array, Uint16Array, Uint32Array, ArrayBuffer});
vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);
const X86Memory = context.module.exports.X86Memory;
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);
const X86CPU = context.X86CPU || context.module.exports;
const mem = new X86Memory(256);
const cpu = new X86CPU(mem, null);
cpu.debug = true;
const kernelBin = fs.readFileSync('/home/janko/dev/code/kernel/kernel.bin');
for (let i = 0; i < kernelBin.length; i++) mem.write8(0x100000 + i, kernelBin[i]);
cpu.regs.eip = 0x100647;
cpu.regs.cs = 0x08;
let count = 0;
while (!cpu.halted && count < 34000) {
    cpu.step();
    count++;
    if (cpu.halted) break;
}
console.log(`\nFinal: EIP=0x${cpu.regs.eip.toString(16)} EAX=0x${cpu.regs.eax.toString(16)} CR0=0x${cpu.cregs.cr0.toString(16)} CR3=0x${cpu.cregs.cr3.toString(16)}`);
