const fs = require('fs');
const vm = require('vm');
const context = vm.createContext({module: {exports: {}}, console, Uint8Array, Uint16Array, Uint32Array, ArrayBuffer});
vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);
const X86Memory = context.module.exports.X86Memory;
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);
const X86CPU = context.X86CPU || context.module.exports;
const mem = new X86Memory(256);
const cpu = new X86CPU(mem, null);
const kernelBin = fs.readFileSync('/home/janko/dev/code/kernel/kernel.bin');
for (let i = 0; i < kernelBin.length; i++) mem.write8(0x100000 + i, kernelBin[i]);
cpu.regs.eip = 0x100647;
cpu.regs.cs = 0x08;
let count = 0;
while (!cpu.halted && count < 34000) {
    const eipBefore = cpu.regs.eip;
    if (eipBefore >= 0x100680 && eipBefore <= 0x100690) {
        const b0 = mem.read8(eipBefore);
        const b1 = mem.read8(eipBefore+1);
        const b2 = mem.read8(eipBefore+2);
        const b3 = mem.read8(eipBefore+3);
        const b4 = mem.read8(eipBefore+4);
        console.log(`[${count}] EIP=0x${eipBefore.toString(16)} bytes: ${b0.toString(16)} ${b1.toString(16)} ${b2.toString(16)} ${b3.toString(16)} ${b4.toString(16)} | EAX=0x${cpu.regs.eax.toString(16)} CR0=0x${cpu.cregs.cr0.toString(16)}`);
    }
    cpu.step();
    count++;
}
console.log(`Halted after ${count} instructions`);
console.log(`EIP=0x${cpu.regs.eip.toString(16)} EAX=0x${cpu.regs.eax.toString(16)} CR0=0x${cpu.cregs.cr0.toString(16)}`);
