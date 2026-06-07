function memoryReadByte(virtaddr) {
    if (!machine || !machine.cpu) return 0xFF;
    try {
        return machine.cpu.readMem(virtaddr, 1);
    } catch (e) {
        return 0xFF;
    }
}

function memoryWriteByte(virtaddr, value) {
    if (!machine || !machine.cpu) return;
    try {
        machine.cpu.writeMem(virtaddr, value, 1);
    } catch (e) {}
}

function setRAMView(virtaddr, size) {
    const lines = size / byte_per_line;
    let dumptxt = "";

    $("#current_memaddr").text(hex(virtaddr, true, 8));

    dumptxt += '<section class="memline heading">' +
        '<div class="memaddr">Address</div>' +
        '<div class="membytes">';
    for (var j = 0; j < byte_per_line; j++) {
        dumptxt += `<div>${hex(j, true, 2)}</div>`;
    }
    dumptxt += '</div><div></div></section>';

    for (var i = 0; i < lines * byte_per_line; i += byte_per_line) {
        let ascii = [];

        dumptxt +=  '<section class="memline">' +
                      '<section class="memaddr">' +
                      hex(virtaddr + i, true, 8) +
                      '</section>' +
                    '<section class="membytes">';

        for (var j = 0; j < byte_per_line; j++) {
            const virt = virtaddr + i + j;
            var byte = memoryReadByte(virt);

            let c = '.';
            if (isPrintable(byte)) {
                c = String.fromCharCode(byte);
                if(c == ' ') c = '&nbsp;';
            }
            ascii.push(`<div class="asciichar" data-addr="${virt}">${c}</div>`);

            var str = byte.toString(16);
            if (str.length == 1)
                str = "0" + str;
            dumptxt += `<div contenteditable data-byte="${byte}" data-addr="${virt}">${str}</div>`;
        }
        dumptxt += '</section>';
        dumptxt += `<section class="asciichars">${ascii.join('')}</section>`;
        dumptxt += '</section>';
    }

    $("#dumpcontent").html(dumptxt);
}

const byte_per_line = 0x10;
var mousepressed = false;

function setClassToASCIIChar(object, classname, add) {
    const index = object.index();
    const asciiline = object.parent().next();
    if (add) {
        asciiline.children().eq(index).addClass(classname);
    } else {
        asciiline.children().eq(index).removeClass(classname);
    }
}

function setClassToMemoryByte(object, classname, add) {
    const index = object.index();
    const memoryline = object.parent().prev();
    const child = memoryline.children().eq(index);
    if (add) {
        child.addClass(classname);
        setMemoryByteAddress(child);
    } else {
        child.removeClass(classname);
    }
}

function setMemoryByteAddress(object) {
    const str = parseInt(object.attr("data-addr"));
    if(!str) return;
    const val = hex(str, true, 8);
    $("#current_memaddr").text(val);
}

$(".membytes").on("mousedown", "div", function() {
    mousepressed = true;
    $(".membytes .selected").removeClass("selected");
    $(this).toggleClass("selected");
});

$(".membytes").on("mouseup", "div", function() {
    mousepressed = false;
});

$(".membytes").on("mouseenter", "div", function() {
    if (mousepressed) {
        $(this).toggleClass("selected");
    }
});

$("#dumpnow").on("click", function() {
    const virtaddr = parseInt($("#dumpaddr").val(), 16);
    const size = parseInt($("#dumpsize").val());
    setRAMView(virtaddr, size);
    localStorage.setItem('dump', JSON.stringify({address:virtaddr.toString(16), size}));
});

$('#dumpaddr, #dumpsize').on('keypress', (evt) => {
    if(evt.keyCode == 13) {
        $('#dumpnow').trigger('click');
    }
});

$("#dumpcontent").on("mouseleave", ".membytes div", function() {
    setClassToASCIIChar($(this), "activefield", false);
});

$("#dumpcontent").on("mouseenter", ".membytes div", function() {
    setClassToASCIIChar($(this), "activefield", true);
    setMemoryByteAddress($(this));
});

$("#dumpcontent").on("mouseleave", ".asciichars div", function() {
    setClassToMemoryByte($(this), "activefield", false);
});

$("#dumpcontent").on("mouseenter", ".asciichars div", function() {
    setClassToMemoryByte($(this), "activefield", true);
});

$("#memdump").on("click", ".dumpline", function() {
    const brkaddr = $(this).data("addr");
    const brk = getBreakpoint(brkaddr);
    if (brk == null) {
        addBreakpoint({ address: brkaddr });
    } else {
        toggleBreakpoint(brkaddr);
    }
});

$("#dumpcontent").on("focusin focusout keyup keydown", ".membytes div[contenteditable]", function(evt) {
    var $this = $(this);
    var value = $this.text();
    switch(evt.type) {
        case 'keyup':
        case 'keydown':
            if(evt.keyCode >= 8 && evt.keyCode <= 9) return;
            if(evt.keyCode == 13) {
                evt.preventDefault();
                $this.blur();
                return;
            }
            if(value.length > 2) {
                evt.preventDefault();
                const t = $this.text().substr(2,1);
                $this.text($this.text().substr(0,2));
                $next = $this.next();
                $next.text(t).trigger('focus');
            }
            break;
        case 'focusin':
            var sel, range;
            range = document.createRange();
            if(value.length == 2) {
                range.selectNodeContents(evt.currentTarget);
            } else {
                range.setStart(evt.currentTarget, 1);
            }
            sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(range);
            break;
        case 'focusout':
            var addr = $this.data('addr');
            var previous = $this.data('byte');
            var current = parseInt($this.text(), 16);
            if(!isNaN(current) && current != previous) {
                memoryWriteByte(addr, current);
                var c = String.fromCharCode(current);
                if(c == ' ') c = '&nbsp;';
                $this.closest('.memline').find(`.asciichars [data-addr=${addr}]`).html(c);
            }
            break;
    }
});

function dumpMemory(options) {
    const { address, size } = options;
    $('#dumpaddr').val(address);
    $('#dumpsize').val(size ?? 256);
    setTimeout(() => {
        $('#dumpnow').trigger('click');
    }, 100);
}

$(function() {
    if(params.dump) {
        const [address,size = 256] = params.dump.split(',');
        dumpMemory({address, size});
        return;
    }
    const addr = localStorage.getItem('dump');
    if(addr) {
        dumpMemory(JSON.parse(addr));
        return;
    }
});
