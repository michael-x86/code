$("#container, #screen").on("click", function() {
    $("#screen").focus();
});

$("#screen").on("focus", function() {
    $(this).css('outline', '1px solid rgba(255,255,255,0.3)');
});

$("#screen").on("blur", function() {
    $(this).css('outline', 'none');
});

$("#screen").on("keydown", function(e) {
    if (!machine || !machine.keyboard) return;
    const key = e.key;
    machine.keyboard.handleKeyEvent(key, true);
    // Only prevent default for printable/alphanumeric keys and arrows
    // Don't block browser shortcuts (F5, Ctrl+R, etc.)
    if (key.length === 1 || key.startsWith('Arrow') ||
        key === 'Backspace' || key === 'Delete' || key === 'Tab' ||
        key === 'Enter' || key === 'Escape' || key === ' ' ||
        key === 'Home' || key === 'End' || key === 'PageUp' || key === 'PageDown' ||
        key === 'Insert') {
        e.preventDefault();
    }
});

$("#screen").on("keyup", function(e) {
    if (!machine || !machine.keyboard) return;
    const key = e.key;
    machine.keyboard.handleKeyEvent(key, false);
    e.preventDefault();
});
