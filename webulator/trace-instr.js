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

// Manually read and decode instructions from 0x100680
console.log('Reading bytes from 0x100680:');
let eip = 0x100680;
for (let i = 0; i < 10; i++) {
    const b = mem.read8(eip + i);
    process.stdout.write(b.toString(16).padStart(2, '0') + ' ');
}
console.log('\n');

// Decode instruction at 0x100680
console.log('Instruction at 0x100680:');
let b0 = mem.read8(0x100680);
let b1 = mem.read8(0x100681);
let b2 = mem.read8(0x100682);
console.log(`Bytes: ${b0.toString(16)} ${b1.toString(16)} ${b2.toString(16)}`);
console.log(`This should be: 0f 22 c0 = MOV CR0, EAX`);

// Check EAX before and after manually
cpu.regs.eip = 0x100680;
cpu.regs.eax = 0x80000001;  // Set EAX to enable paging
console.log(`\nBefore MOV CR0, EAX: 0x${cpu.regs.eax.toString(16)}`);
// Manually execute MOV CR0, EAX
cpu.cregs.cr0 = cpu.regs.eax;
cpu.regs.eip += 3;
console.log(`After MOV CR0, CR0: 0x${cpu.cregs.cr0.toString(16)}, EIP: 0x${cpu.regs.eip.toString(16)}`);

// Now at 0x100683, read MOV EAX instruction
b0 = mem.read8(0x100683);
b1 = mem.read8(0x100684);
b2 = mem.read8(0x100685);
let b3 = mem.read8(0x100686);
let b4 = mem.read8(0x100687);
console.log(`\nBytes at 0x100683: ${b0.toString(16)} ${b1.toString(16)} ${b2.toString(16)} ${b3.toString(16)} ${b4.toString(16)}`);
console.log(`This should be: b8 8a 06 10 c0 = MOV EAX, 0xc010068a`);
