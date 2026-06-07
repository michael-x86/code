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

    try {
        const kernelResp = await fetch(`${base}/kernel.bin`);
        if (kernelResp.ok) {
            const data = await kernelResp.arrayBuffer();
            machine.mem.loadBinary(data, 0x100000);
            machine.cpu.regs.eip = 0x100000;
            $("#kernready").addClass("ready");
            kernelLoaded = true;
            console.log(`[webulator] Auto-loaded kernel.bin (${data.byteLength} bytes)`);
        }
    } catch (e) {
        console.log('[webulator] No pre-built kernel.bin found, use manual upload');
    }

    try {
        const imgResp = await fetch(`${base}/os.img`);
        if (imgResp.ok) {
            const data = await imgResp.arrayBuffer();
            machine.loadDiskImage(data);
            machine.mem.loadDiskImage(data);
            $("#diskready").addClass("ready");
            diskLoaded = true;
            console.log(`[webulator] Auto-loaded os.img (${data.byteLength} bytes)`);
        }
    } catch (e) {
        console.log('[webulator] No pre-built os.img found, use manual upload');
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
