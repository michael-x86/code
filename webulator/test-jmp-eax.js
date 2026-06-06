const fs = require('fs');
const vm = require('vm');
const context = vm.createContext({module: {exports: {}}, console, Uint8Array, Uint16Array, Uint32Array, ArrayBuffer});
vm.runInContext(fs.readFileSync('hardemu/memory.js', 'utf8'), context);
const X86Memory = context.module.exports.X86Memory;
vm.runInContext(fs.readFileSync('hardemu/x86cpu.js', 'utf8'), context);
const X86CPU = context.X86CPU || context.module.exports;
const mem = new X86Memory(256);
const cpu = new X86CPU(mem, null);

// Load kernel
const kernelBin = fs.readFileSync('/home/janko/dev/code/kernel/kernel.bin');
for (let i = 0; i < kernelBin.length; i++) mem.write8(0x100000 + i, kernelBin[i]);

// Manually set up state and trace
cpu.regs.eip = 0x100680;
cpu.cregs.cr0 = 0x0;
cpu.regs.eax = 0x800559a5;  // Value before MOV CR0, EAX

console.log('Before execution:');
console.log(`EIP=0x${cpu.regs.eip.toString(16)}, EAX=0x${cpu.regs.eax.toString(16)}, CR0=0x${cpu.cregs.cr0.toString(16)}`);

// Execute MOV CR0, EAX (0x100680: 0f 22 c0)
console.log('\nExecuting MOV CR0, EAX...');
cpu.step();
console.log(`After: EIP=0x${cpu.regs.eip.toString(16)}, EAX=0x${cpu.regs.eax.toString(16)}, CR0=0x${cpu.cregs.cr0.toString(16)}`);

// Execute MOV EAX, 0xc010068a (0x100683: b8 8a 06 10 c0)
console.log('\nExecuting MOV EAX, 0xc010068a...');
cpu.step();
console.log(`After: EIP=0x${cpu.regs.eip.toString(16)}, EAX=0x${cpu.regs.eax.toString(16)}`);

// Execute JMP EAX (0x100688: ff e0)
console.log('\nExecuting JMP EAX...');
cpu.step();
console.log(`After: EIP=0x${cpu.regs.eip.toString(16)}`);
