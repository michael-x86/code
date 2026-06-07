function setRegView() {
    if (!machine || !machine.cpu) return;
    const cpu = machine.cpu;
    const state = machine.getCPUState();

    $("#reg_eax").text('0x' + hex32(cpu.regs.eax));
    $("#reg_ebx").text('0x' + hex32(cpu.regs.ebx));
    $("#reg_ecx").text('0x' + hex32(cpu.regs.ecx));
    $("#reg_edx").text('0x' + hex32(cpu.regs.edx));
    $("#reg_esi").text('0x' + hex32(cpu.regs.esi));
    $("#reg_edi").text('0x' + hex32(cpu.regs.edi));
    $("#reg_ebp").text('0x' + hex32(cpu.regs.ebp));
    $("#reg_esp").text('0x' + hex32(cpu.regs.esp));
    $("#reg_eip").text('0x' + hex32(cpu.regs.eip));

    const eflags = cpu.eflags;
    var flags = "";
    flags += (eflags & (1 << 0))  ? "CF " : "";
    flags += (eflags & (1 << 2))  ? "PF " : "";
    flags += (eflags & (1 << 4))  ? "AF " : "";
    flags += (eflags & (1 << 6))  ? "ZF " : "";
    flags += (eflags & (1 << 7))  ? "SF " : "";
    flags += (eflags & (1 << 8))  ? "TF " : "";
    flags += (eflags & (1 << 9))  ? "IF " : "";
    flags += (eflags & (1 << 10)) ? "DF " : "";
    flags += (eflags & (1 << 11)) ? "OF " : "";
    if (flags === "") flags = "----";
    $("#reg_eflags").text(flags);

    $("#reg_cs").text(hex16(cpu.segregs.cs));
    $("#reg_ds").text(hex16(cpu.segregs.ds));
    $("#reg_es").text(hex16(cpu.segregs.es));
    $("#reg_fs").text(hex16(cpu.segregs.fs));
    $("#reg_gs").text(hex16(cpu.segregs.gs));
    $("#reg_ss").text(hex16(cpu.segregs.ss));

    $("#reg_cr0").text(hex32(cpu.cregs.cr0));
    $("#reg_cr3").text(hex32(cpu.cregs.cr3));

    $("#tstates").text(state.tstates);

    disassembleAndShow();
}

function clearRegView() {
    const ids = ["#reg_eax", "#reg_ebx", "#reg_ecx", "#reg_edx",
                 "#reg_esi", "#reg_edi", "#reg_ebp", "#reg_esp",
                 "#reg_eip", "#reg_eflags",
                 "#reg_cs", "#reg_ds", "#reg_es", "#reg_fs", "#reg_gs", "#reg_ss",
                 "#reg_cr0", "#reg_cr3",
                 "#tstates"];
    ids.forEach(id => $(id).text(""));
}

$(".regaddr").click(function() {
    const text = $(this).text();
    if (text.startsWith('0x')) {
        const virtaddr = parseInt(text, 16);
        if (!isNaN(virtaddr)) {
            const size = 256;
            setRAMView(virtaddr, size);
            $("#memory-tab").click();
        }
    }
});
