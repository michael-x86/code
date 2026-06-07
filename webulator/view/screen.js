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
