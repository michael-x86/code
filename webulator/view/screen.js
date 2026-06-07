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
    e.preventDefault();
});

$("#screen").on("keyup", function(e) {
    if (!machine || !machine.keyboard) return;
    const key = e.key;
    machine.keyboard.handleKeyEvent(key, false);
    e.preventDefault();
});
