$("#container, #screen").on("click", function() {
    $("#screen").focus();
});

$("#screen").on("focus", function() {
    $(this).css('outline', '1px solid rgba(255,255,255,0.3)');
});

$("#screen").on("blur", function() {
    $(this).css('outline', 'none');
});

// Also forward document keystrokes to the PS/2 keyboard when no input/textarea
// is focused, so the user can type without clicking the canvas first.
$(document).on("keydown", function(e) {
    if (!machine || !machine.keyboard) return;
    if ($(e.target).is("input, textarea, [contenteditable]")) return;
    if (e.key.startsWith('F') && e.key.length <= 3) return; // F-keys for UI
    if (e.ctrlKey || e.altKey || e.metaKey) return;
    machine.keyboard.handleKeyEvent(e.key, true);
    e.preventDefault();
});

$(document).on("keyup", function(e) {
    if (!machine || !machine.keyboard) return;
    if ($(e.target).is("input, textarea, [contenteditable]")) return;
    if (e.key.startsWith('F') && e.key.length <= 3) return;
    if (e.ctrlKey || e.altKey || e.metaKey) return;
    machine.keyboard.handleKeyEvent(e.key, false);
    e.preventDefault();
});

$("#screen").on("keydown", function(e) {
    if (!machine || !machine.keyboard) return;
    const key = e.key;
    machine.keyboard.handleKeyEvent(key, true);
    e.preventDefault();
});

$("#screen").on("keyup", function(e) {
    if (!machine || !machine.keyboard) return;
    const key = e.key;
    machine.keyboard.handleKeyEvent(key, false);
    e.preventDefault();
});
