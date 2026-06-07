$("#step").on("click",     () => { if (machine) machine.step(); });
$("#reset").on("click",    () => { if (machine) machine.reset(); });
$("#continue").on("click", () => { if (machine) { machine.cont(); showPauseView(); } });
$("#pause").on("click",    () => { if (machine) { machine.stop(); showContinueView(); } });
$("#clean").on("click",    () => {
    if (machine) machine.reset();
    resetKernel();
});

$("#speed-slider").on("input", function() {
    const val = parseInt($(this).val());
    $("#speed-label").text(val + 'x');
    if (machine) {
        machine.speedDivider = val;
        if (machine.running) {
            machine.stop();
            machine.start();
        }
    }
});

function showPauseView() {
    $("#continue").hide();
    $("#pause").show();
}

function showContinueView() {
    $("#continue").show();
    $("#pause").hide();
}

function saveMenuState() {
    const menus = {};
    $('.menu').each(function() {
        const id = $(this).attr('id');
        menus[id] = $(this).hasClass('visible');
    });
    localStorage.setItem('menus', JSON.stringify(menus));
}

$(".menutitle").on('click', function(e) {
    e.preventDefault();
    e.stopPropagation();
    $(this).parent().toggleClass('visible');
    saveMenuState();
});

$("#theme").on("change", function() {
    $(":root").removeClass();
    $(":root").addClass($(this).val());
})

$('#canvas-smooth-val').on('change', (e) => {
    const smooth = e.currentTarget.checked;
    localStorage.setItem('canvas-smooth', JSON.stringify(smooth));
    if(smooth) {
        $('#screen').addClass('no-pixels');
    } else {
        $('#screen').removeClass('no-pixels');
    }
})

$('#screen-capture').on('click', () => {
    const canvas = document.getElementById('screen');
    const image = canvas.toDataURL();
    const link = document.createElement('a');
    link.href = image;
    link.download = 'screenshot.png';
    link.click();
});

$('#theater-mode').on('click', () => {
    $('#toppanel').toggleClass('theater-mode');
});

window.fullscreenMode = false;
$('#fullscreen-mode').on('click', () => {
    const canvas = document.querySelector('#container canvas');
    if(document.fullscreenElement && document.exitFullscreen) {
        document.exitFullscreen();
    } else {
        if(canvas && canvas.requestFullscreen) {
            canvas.requestFullscreen({
                navigationUI: "hide",
            });
        }
    }
});

$("#console-paste").on("click", async () => {
    $('#console-paste').blur();
    let text = null;
    try {
        text = await navigator.clipboard.readText();
    } catch (err) {
        alert("Failed to read clipboard");
    }
    if (!text || !machine || !machine.keyboard) return;
    for (const ch of text) {
        const key = ch === '\n' ? 'Enter' : ch === '\t' ? 'Tab' : ch;
        machine.keyboard.handleKeyEvent(key, true);
        machine.keyboard.handleKeyEvent(key, false);
    }
    $('#screen').focus();
});

$('#container .close').on('click', () => {
    $('#toppanel').toggleClass('theater-mode');
})

jQuery(() => {
    $('#continue').hide();
    $('#pause').show();

    let menus = JSON.parse(localStorage.getItem('menus')) ?? {};
    Object.entries(menus).map((entry) => {
        const [k,v] = entry;
        if(v) $(`#${k}`).addClass('visible');
    });

    const smooth = JSON.parse(localStorage.getItem('canvas-smooth') ?? false);
    $('#canvas-smooth-val').attr('checked', smooth).trigger('change');

    const canvas = document.querySelector('#container canvas');
    if(canvas && canvas.requestFullscreen) {
        $('#fullscreen-mode').show();
    }
});
