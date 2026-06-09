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

// Forward terminal input to both PS/2 keyboard and serial port (COM1)
terminal.onData((data) => {
    if (!machine) return;
    machine.writeSerial(data);
    for (const ch of data) {
        let key = ch;
        if (key === '\r') key = 'Enter';
        else if (key === '\x7f') key = 'Backspace';
        else if (key === '\t') key = 'Tab';
        else if (key === '\x1b') key = 'Escape';
        machine.keyboard.handleKeyEvent(key, true);
        machine.keyboard.handleKeyEvent(key, false);
    }
});

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
var regLog = [];

function formatRegState() {
    if (!machine) return '';
    const c = machine.cpu;
    return `T=${machine.tstates} EIP=${hex32(c.regs.eip)} ` +
        `EAX=${hex32(c.regs.eax)} EBX=${hex32(c.regs.ebx)} ` +
        `ECX=${hex32(c.regs.ecx)} EDX=${hex32(c.regs.edx)} ` +
        `ESI=${hex32(c.regs.esi)} EDI=${hex32(c.regs.edi)} ` +
        `EBP=${hex32(c.regs.ebp)} ESP=${hex32(c.regs.esp)} ` +
        `EFL=${hex32(c.eflags)} ` +
        `CS=${hex16(c.segregs.cs)} DS=${hex16(c.segregs.ds)} ` +
        `SS=${hex16(c.segregs.ss)} ` +
        `CR0=${hex32(c.cregs.cr0)} CR3=${hex32(c.cregs.cr3)} ` +
        ` halted=${c.halted}`;
}

function downloadRegLog() {
    if (regLog.length === 0) return;
    const blob = new Blob([regLog.join('\n')], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `reglog-${Date.now()}.txt`;
    a.click();
    URL.revokeObjectURL(url);
}

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
        terminal.write(String.fromCharCode(char));
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

    try {
        const resp = await fetch(`${base}/kernel.bin`);
        if (resp.ok) {
            const data = await resp.arrayBuffer();
            machine.mem.loadBinary(data, 0x100000);

            // Set up machine state as bootloader would:
            // Flat protected mode with kernel at 1MB mark
            
            // Bootloader handoff: store FS disk parameters at physical 0x500
            // (normally written by bootloader.asm before jumping to kernel)
            machine.mem.write32(0x500, 0x01F0);   // fs_ata_base = primary IDE
            machine.mem.write8(0x504, 0xE0);       // fs_ata_drive = master
            machine.mem.write32(0x508, 40);        // fs_base_lba = after boot+image
            
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

            // Initial IDT was installed by machine_x86.setupInitialIDT() 
            // during X86Machine.init() — it survives here because loadBinary
            // writes at 0x100000+, far from the IDT at 0x5000 and handler at 0x6000.

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
        showContinueView();
        updateRegView();
        machine.onFrame = () => {
            setRegView();
            if ($('#reg-log-toggle').is(':checked')) {
                regLog.push(formatRegState());
                $('#reg-log-count').text(regLog.length + ' lines');
            }
        };
    }
}

function updateRegView() {
    if (machine) {
        setRegView();
    }
}

$('#reg-log-save').on('click', downloadRegLog);
$('#reg-log-clear').on('click', () => {
    regLog = [];
    $('#reg-log-count').text('');
});

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
