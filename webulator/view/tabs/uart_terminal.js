let sending = false;
const sending_message = "Sending";
let dots_count = 3;

$("#uart-file-send").on("click", function() {
    const $button = $(this);
    if (sending) return;

    let file = $("#uart-file")[0].files[0];
    if (!file) return;

    let reader = new FileReader();
    reader.addEventListener('load', function(e) {
        let binary = e.target.result;
        sending = true;
        $button.text(sending_message);

        const interval = setInterval(function() {
            if (dots_count == 3) dots_count = 0;
            else dots_count++;
            $button.text(sending_message + '.'.repeat(dots_count));
        }, 333);

        setTimeout(function() {
            for (let i = 0; i < binary.length; i++) {
                const byte = binary.charCodeAt(i);
                if (machine && machine.onSerialOutput) {
                    machine.onSerialOutput(byte);
                }
            }
            sending = false;
            clearInterval(interval);
            $button.text('Send');
        }, 10);
    });
    reader.readAsBinaryString(file);
});

$('#uart-cols').on('change', function() {
    UART_SIZE.cols = $(this).val();
    terminal.resize(UART_SIZE.cols, UART_SIZE.rows);
});

$('#uart-rows').on('change', function() {
    UART_SIZE.rows = $(this).val();
    terminal.resize(UART_SIZE.cols, UART_SIZE.rows);
});

$('#uart-char').on('keypress', (e) => {
    if(e.keyCode == 13) {
        $("#uart-char-send").trigger('click');
    }
});

$("#uart-char-send").on("click", function() {
    if (sending) return;
    const val = parseInt($("#uart-char").val());
    if (machine && machine.onSerialOutput) {
        machine.onSerialOutput(val);
    }
});

$("#clearterm").on("click", function() {
    terminal.reset();
});

$("#baudrate").on("change", function() {
    const baudrate = $(this).val();
    console.log('Baudrate changed to:', baudrate);
});
