const KB = 1024;
const CPUFREQ = 10000000;
const TSTATES_US = 1/CPUFREQ * 1000000;

function us_to_tstates(us) {
    return Math.floor(us / TSTATES_US);
}

const UART_SIZE = {
    cols: 80,
    rows: 24,
};

var terminal = new Terminal({
    ...UART_SIZE,
    screenKeys: true,
    convertEol: true,
    cursorBlink: true,
    fontSize: 14,
});
$('#uart-cols').val(UART_SIZE.cols);
$('#uart-rows').val(UART_SIZE.rows);
terminal.open(document.getElementById('terminal'));

document.addEventListener('keydown', function(event) {
    var handled = false;
    const binding = {
        'F4': $("#theater-mode"),
        'F5': $("#fullscreen-mode"),
        'F9': $(".cpuexec:visible"),
        'F10': $("#step")
    };
    if (binding[event.key]) {
        binding[event.key].click();
        handled = true;
    }
    if (handled) {
        event.preventDefault();
    }
});

var machine = null;
var disassembler = null;
const popout = new Popup();

const params = parseQueryParams(window.location.search);

$('#screen').focus();
$(window).on('focus', () => {
    $('#screen').focus();
})

function initEmulator() {
    const canvas = document.getElementById('screen');
    canvas.width = 640;
    canvas.height = 400;
    machine = new X86Machine();
    machine.setCanvas(canvas);

    machine.onSerialOutput = (char) => {
        if (char === 0x0A) {
            terminal.write('\r\n');
        } else if (char >= 32 && char <= 126) {
            terminal.write(String.fromCharCode(char));
        }
    };

    machine.onVgaUpdate = () => {
        machine.vga.render();
    };

    console.log('x86 Emulator initialized');
    updateRegView();
    loadBuiltBinaries();
}

async function loadBuiltBinaries() {
    const base = 'kernel-bin';
    let kernelLoaded = false;
    let diskLoaded = false;

    const IDT_BASE = 0x5000;
    const HANDLER_ADDR = 0x6000;

    try {
        const resp = await fetch(`${base}/kernel.bin`);
        if (resp.ok) {
            const data = await resp.arrayBuffer();
            machine.mem.loadBinary(data, 0x100000);

            // Set up machine state as bootloader would:
            // Flat protected mode with kernel at 1MB mark
            machine.cpu.regs.eip = 0x100000;
            machine.cpu.segregs.cs = 0x08;
            machine.cpu.segregs.ds = 0x10;
            machine.cpu.segregs.es = 0x10;
            machine.cpu.segregs.fs = 0x10;
            machine.cpu.segregs.gs = 0x10;
            machine.cpu.segregs.ss = 0x10;
            machine.cpu.regs.esp = 0x200000;
            machine.cpu.regs.ebp = 0x200000;
            machine.cpu.cregs.cr0 = 0x00000001;

            // Set up a minimal IDT so exceptions don't crash
            machine.mem.write8(HANDLER_ADDR, 0xFA);
            machine.mem.write8(HANDLER_ADDR + 1, 0xF4);
            for (let i = 0; i < 256; i++) {
                const entryAddr = IDT_BASE + (i * 8);
                machine.mem.write16(entryAddr, HANDLER_ADDR & 0xFFFF);
                machine.mem.write16(entryAddr + 2, 0x0008);
                machine.mem.write8(entryAddr + 4, 0x00);
                machine.mem.write8(entryAddr + 5, 0x8E);
                machine.mem.write8(entryAddr + 6, (HANDLER_ADDR >> 16) & 0xFF);
                machine.mem.write8(entryAddr + 7, (HANDLER_ADDR >> 24) & 0xFF);
            }
            machine.cpu.idtBase = IDT_BASE;
            machine.cpu.idtLimit = 0x7FF;

            $("#kernready").addClass("ready");
            kernelLoaded = true;
            console.log(`[webulator] Auto-loaded kernel.bin (${data.byteLength} bytes)`);
        } else {
            console.log(`[webulator] kernel.bin not found (HTTP ${resp.status}), use manual upload`);
        }
    } catch (e) {
        console.log('[webulator] Cannot fetch kernel.bin, use manual upload');
    }

    try {
        const resp = await fetch(`${base}/os.img`);
        if (resp.ok) {
            const data = await resp.arrayBuffer();
            machine.loadDiskImage(data);
            machine.mem.loadDiskImage(data);
            $("#diskready").addClass("ready");
            diskLoaded = true;
            console.log(`[webulator] Auto-loaded os.img (${data.byteLength} bytes)`);
        } else {
            console.log(`[webulator] os.img not found (HTTP ${resp.status}), use manual upload`);
        }
    } catch (e) {
        console.log('[webulator] Cannot fetch os.img, use manual upload');
    }

    if (kernelLoaded) {
        showPauseView();
        updateRegView();
        machine.start();
    }
}

function updateRegView() {
    if (machine) {
        setRegView();
    }
}

$(initEmulator);

function hex8(v) {
    return ('0' + (v & 0xFF).toString(16).toUpperCase()).slice(-2);
}

function hex16(v) {
    return ('0000' + (v & 0xFFFF).toString(16).toUpperCase()).slice(-4);
}

function hex32(v) {
    return ('00000000' + ((v >>> 0).toString(16).toUpperCase())).slice(-8);
}
