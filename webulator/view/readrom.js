function loadKernel(file_rom) {
    if (!file_rom) { console.log('loadKernel: no file selected'); return; }
    let reader = new FileReader();
    reader.onload = function(e) {
        const data = new Uint8Array(e.target.result);
        if (machine && machine.mem) {
            // Re-install initial IDT before loading (validates the CPU can
            // survive any exception during early kernel boot).  loadBinary
            // writes at 0x100000+, far from the IDT at 0x5000, so this is
            // safe to call before loading.
            machine.setupInitialIDT();

            machine.mem.loadBinary(data, 0x100000);
            machine.cpu.regs.eip = 0x100000;
            $("#kernready").addClass("ready");
            console.log(`Kernel loaded: ${data.length} bytes at 0x100000`);
        } else {
            console.error('loadKernel: machine not ready');
        }
    };
    reader.onerror = function(e) {
        console.error('loadKernel: file read error', e);
    };
    reader.readAsArrayBuffer(file_rom);
}

function loadDisk(file_disk) {
    if (!file_disk) { console.log('loadDisk: no file selected'); return; }
    let reader = new FileReader();
    reader.onload = function(e) {
        const data = new Uint8Array(e.target.result);
        if (machine) {
            machine.loadDiskImage(data.buffer);
            machine.mem.loadDiskImage(data.buffer);
            $("#diskready").addClass("ready");
            console.log(`Disk image loaded: ${data.length} bytes`);
        }
    };
    reader.onerror = function(e) {
        console.error('loadDisk: file read error', e);
    };
    reader.readAsArrayBuffer(file_disk);
}

function loadBootloader(file_boot) {
    if (!file_boot) { console.log('loadBootloader: no file selected'); return; }
    let reader = new FileReader();
    reader.onload = function(e) {
        const data = new Uint8Array(e.target.result);
        if (machine && machine.mem) {
            machine.setupInitialIDT();
            machine.mem.loadBinary(data, 0x7C00);
            machine.cpu.regs.eip = 0x7C00;
            $("#bootready").addClass("ready");
            console.log(`Bootloader loaded: ${data.length} bytes at 0x7C00`);
        }
    };
    reader.onerror = function(e) {
        console.error('loadBootloader: file read error', e);
    };
    reader.readAsArrayBuffer(file_boot);
}

$("#read-button").on('click', function() {
    loadKernel($("#file-rom")[0].files[0]);
    loadDisk($("#file-disk")[0].files[0]);
    loadBootloader($("#file-boot")[0].files[0]);
    $("#continue").click();
});

$("#romadvanced a").click(() => {
    $("#romfile").toggle(500);
});

function resetKernel() {
    $("#romfile [type=file]").val("");
    $(".status").removeClass("ready");
}
