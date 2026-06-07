function loadKernel(file_rom) {
    if (!file_rom) return;
    let reader = new FileReader();
    reader.onload = function(e) {
        const data = new Uint8Array(e.target.result);
        if (machine && machine.mem) {
            machine.mem.loadBinary(data, 0x100000);
            machine.cpu.regs.eip = 0x100000;
            $("#kernready").addClass("ready");
            console.log(`Kernel loaded: ${data.length} bytes at 0x100000`);
        }
    };
    reader.readAsArrayBuffer(file_rom);
}

function loadDisk(file_disk) {
    if (!file_disk) return;
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
    reader.readAsArrayBuffer(file_disk);
}

function loadBootloader(file_boot) {
    if (!file_boot) return;
    let reader = new FileReader();
    reader.onload = function(e) {
        const data = new Uint8Array(e.target.result);
        if (machine && machine.mem) {
            machine.mem.loadBinary(data, 0x7C00);
            machine.cpu.regs.eip = 0x7C00;
            $("#bootready").addClass("ready");
            console.log(`Bootloader loaded: ${data.length} bytes at 0x7C00`);
        }
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
