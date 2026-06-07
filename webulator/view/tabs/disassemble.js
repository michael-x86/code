var opcodeNames = {
    0x00: 'ADD r/m8,r8', 0x01: 'ADD r/m32,r32',
    0x02: 'ADD r8,r/m8', 0x03: 'ADD r32,r/m32',
    0x04: 'ADD AL,imm8', 0x05: 'ADD EAX,imm32',
    0x08: 'OR r/m8,r8', 0x09: 'OR r/m32,r32',
    0x0A: 'OR r8,r/m8', 0x0B: 'OR r32,r/m32',
    0x0C: 'OR AL,imm8', 0x0D: 'OR EAX,imm32',
    0x10: 'ADC r/m8,r8', 0x11: 'ADC r/m32,r32',
    0x12: 'ADC r8,r/m8', 0x13: 'ADC r32,r/m32',
    0x20: 'AND r/m8,r8', 0x21: 'AND r/m32,r32',
    0x22: 'AND r8,r/m8', 0x23: 'AND r32,r/m32',
    0x25: 'AND EAX,imm32',
    0x28: 'SUB r/m8,r8', 0x29: 'SUB r/m32,r32',
    0x2A: 'SUB r8,r/m8', 0x2B: 'SUB r32,r/m32',
    0x2D: 'SUB EAX,imm32',
    0x31: 'XOR r/m32,r32', 0x33: 'XOR r32,r/m32',
    0x35: 'XOR EAX,imm32',
    0x38: 'CMP r/m8,r8', 0x39: 'CMP r/m32,r32',
    0x3A: 'CMP r8,r/m8', 0x3B: 'CMP r32,r/m32',
    0x3C: 'CMP AL,imm8', 0x3D: 'CMP EAX,imm32',
    0x40: 'INC EAX', 0x41: 'INC ECX', 0x42: 'INC EDX', 0x43: 'INC EBX',
    0x44: 'INC ESP', 0x45: 'INC EBP', 0x46: 'INC ESI', 0x47: 'INC EDI',
    0x48: 'DEC EAX', 0x49: 'DEC ECX', 0x4A: 'DEC EDX', 0x4B: 'DEC EBX',
    0x4C: 'DEC ESP', 0x4D: 'DEC EBP', 0x4E: 'DEC ESI', 0x4F: 'DEC EDI',
    0x50: 'PUSH EAX', 0x51: 'PUSH ECX', 0x52: 'PUSH EDX', 0x53: 'PUSH EBX',
    0x54: 'PUSH ESP', 0x55: 'PUSH EBP', 0x56: 'PUSH ESI', 0x57: 'PUSH EDI',
    0x58: 'POP EAX', 0x59: 'POP ECX', 0x5A: 'POP EDX', 0x5B: 'POP EBX',
    0x5C: 'POP ESP', 0x5D: 'POP EBP', 0x5E: 'POP ESI', 0x5F: 'POP EDI',
    0x60: 'PUSHAD', 0x61: 'POPAD',
    0x68: 'PUSH imm32', 0x6A: 'PUSH imm8',
    0x70: 'JO', 0x71: 'JNO', 0x72: 'JB', 0x73: 'JNB',
    0x74: 'JE', 0x75: 'JNE', 0x76: 'JBE', 0x77: 'JA',
    0x78: 'JS', 0x79: 'JNS', 0x7A: 'JP', 0x7B: 'JNP',
    0x7C: 'JL', 0x7D: 'JGE', 0x7E: 'JLE', 0x7F: 'JG',
    0x84: 'TEST r/m8,r8', 0x85: 'TEST r/m32,r32',
    0x88: 'MOV r/m8,r8', 0x89: 'MOV r/m32,r32',
    0x8A: 'MOV r8,r/m8', 0x8B: 'MOV r32,r/m32',
    0x8C: 'MOV r/m16,seg', 0x8D: 'LEA r32,m',
    0x8E: 'MOV seg,r/m16',
    0x90: 'NOP',
    0x9C: 'PUSHF', 0x9D: 'POPF',
    0xA1: 'MOV EAX,moffs', 0xA2: 'MOV moffs8,AL', 0xA3: 'MOV moffs32,EAX',
    0xA4: 'MOVSB', 0xA5: 'MOVSD',
    0xA6: 'CMPSB', 0xA7: 'CMPSD',
    0xA8: 'TEST AL,imm8', 0xA9: 'TEST EAX,imm32',
    0xAA: 'STOSB', 0xAB: 'STOSD',
    0xAC: 'LODSB', 0xAD: 'LODSD',
    0xB0: 'MOV AL,imm8', 0xB1: 'MOV CL,imm8',
    0xB2: 'MOV DL,imm8', 0xB3: 'MOV BL,imm8',
    0xB4: 'MOV AH,imm8', 0xB5: 'MOV CH,imm8',
    0xB6: 'MOV DH,imm8', 0xB7: 'MOV BH,imm8',
    0xB8: 'MOV EAX,imm32', 0xB9: 'MOV ECX,imm32',
    0xBA: 'MOV EDX,imm32', 0xBB: 'MOV EBX,imm32',
    0xBC: 'MOV ESP,imm32', 0xBD: 'MOV EBP,imm32',
    0xBE: 'MOV ESI,imm32', 0xBF: 'MOV EDI,imm32',
    0xC0: 'GRP2 r/m8,imm8', 0xC1: 'GRP2 r/m32,imm8',
    0xC2: 'RET imm16', 0xC3: 'RET',
    0xC6: 'MOV r/m8,imm8', 0xC7: 'MOV r/m32,imm32',
    0xCC: 'INT3', 0xCD: 'INT imm8',
    0xCF: 'IRETD',
    0xD0: 'GRP2 r/m8,1', 0xD1: 'GRP2 r/m32,1',
    0xD2: 'GRP2 r/m8,CL', 0xD3: 'GRP2 r/m32,CL',
    0xE0: 'LOOPNE', 0xE1: 'LOOPE', 0xE2: 'LOOP',
    0xE4: 'IN AL,imm8', 0xE5: 'IN EAX,imm8',
    0xE6: 'OUT imm8,AL', 0xE7: 'OUT imm8,EAX',
    0xE8: 'CALL rel32', 0xE9: 'JMP rel32',
    0xEB: 'JMP rel8',
    0xEC: 'IN AL,DX', 0xED: 'IN EAX,DX',
    0xEE: 'OUT DX,AL', 0xEF: 'OUT DX,EAX',
    0xF4: 'HLT',
    0xF6: 'GRP3 r/m8', 0xF7: 'GRP3 r/m32',
    0xFA: 'CLI', 0xFB: 'STI',
    0xFC: 'CLD', 0xFD: 'STD',
    0xFF: 'GRP5',
};

function getOpcodeName(opcode) {
    return opcodeNames[opcode] || ('DB 0x' + opcode.toString(16).toUpperCase());
}

function getInstructionLength(opcode) {
    if (opcode === 0x66 || opcode === 0x67 || opcode === 0xF0 ||
        opcode === 0xF2 || opcode === 0xF3 ||
        (opcode >= 0x26 && opcode <= 0x3E && (opcode & 0x7) === 6)) {
        return 1;
    }
    if (opcode === 0x0F) return 2;
    if (opcode >= 0x50 && opcode <= 0x5F) return 1;
    if (opcode >= 0x40 && opcode <= 0x4F) return 1;
    if (opcode === 0x90 || opcode === 0xF4) return 1;
    if (opcode === 0xC3 || opcode === 0xCB || opcode === 0xCF) return 1;
    if (opcode === 0x9C || opcode === 0x9D) return 1;
    if (opcode === 0x60 || opcode === 0x61) return 1;
    if (opcode === 0xFA || opcode === 0xFB) return 1;
    if (opcode === 0xFC || opcode === 0xFD) return 1;
    if (opcode === 0xCC) return 1;
    if (opcode === 0xCD) return 2;
    if (opcode === 0xE0 || opcode === 0xE1 || opcode === 0xE2) return 2;
    if (opcode === 0xC2) return 3;
    if (opcode >= 0x70 && opcode <= 0x7F) return 2;
    if (opcode === 0xEB) return 2;
    if (opcode === 0x6A) return 2;
    if (opcode === 0xE9 || opcode === 0xE8) return 5;
    if (opcode === 0xE6 || opcode === 0xE4) return 2;
    if (opcode === 0xE7 || opcode === 0xE5) return 2;
    if (opcode === 0xEE || opcode === 0xEC) return 1;
    if (opcode === 0xEF || opcode === 0xED) return 1;
    if (opcode === 0x68) return 5;
    if (opcode === 0xA1 || opcode === 0xA2 || opcode === 0xA3) return 5;
    if (opcode >= 0xB8 && opcode <= 0xBF) return 5;
    if (opcode >= 0xB0 && opcode <= 0xB7) return 2;
    if (opcode >= 0xA4 && opcode <= 0xAD) return 1;
    if (opcode >= 0x88 && opcode <= 0x8E) return 2;
    if (opcode === 0x8D) return 2;
    if (opcode >= 0x00 && opcode <= 0x05) return 2;
    if (opcode >= 0x08 && opcode <= 0x0D) return 2;
    if (opcode >= 0x10 && opcode <= 0x13) return 2;
    if (opcode >= 0x20 && opcode <= 0x23) return 2;
    if (opcode === 0x25) return 5;
    if (opcode >= 0x28 && opcode <= 0x2D) return 2;
    if (opcode === 0x31 || opcode === 0x33 || opcode === 0x35) return 2;
    if (opcode >= 0x38 && opcode <= 0x3D) return 2;
    if (opcode === 0xA8) return 2;
    if (opcode === 0xA9) return 5;
    if (opcode === 0x84 || opcode === 0x85) return 2;
    if (opcode === 0xC6 || opcode === 0xC7) return 6;
    if (opcode === 0xC0 || opcode === 0xC1) return 3;
    if (opcode === 0xD0 || opcode === 0xD1) return 2;
    if (opcode === 0xD2 || opcode === 0xD3) return 2;
    if (opcode === 0xF6 || opcode === 0xF7) return 2;
    if (opcode === 0xFF) return 2;
    return 2;
}

function disassembleAndShow() {
    if (!machine || !machine.cpu) return;
    const cpu = machine.cpu;
    const pc = cpu.regs.eip >>> 0;
    const instructions = 20;
    const bytes = instructions * 8;

    var memory = [];
    for (var i = 0; i < bytes + 8; i++) {
        try {
            memory.push(cpu.readMem(pc + i, 1));
        } catch (e) {
            memory.push(0);
        }
    }

    var dumptxt = '';
    var addr = pc;
    for (var i = 0; i < instructions; i++) {
        var cssclass = (addr === pc) ? "activeline dumpline" : "dumpline";
        var line = addr === pc ? "▶ " : "  ";

        const opcode = memory[((addr - pc) >>> 0)];
        var mnem = getOpcodeName(opcode);

        line += '0x' + hex32(addr) + '  ';
        for (var j = 0; j < 8 && ((addr + j - pc) >>> 0) < memory.length; j++) {
            line += hex8(memory[((addr + j - pc) >>> 0)]) + ' ';
        }

        const instrLen = getInstructionLength(opcode);
        line += '  ' + mnem;
        dumptxt += `<div data-addr="${addr}" class="${cssclass}">${line}</div>`;
        addr = (addr + instrLen) >>> 0;
    }

    $("#memdump").html(dumptxt);
}
