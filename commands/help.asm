; help — print list of available commands
[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp, .get_base

    lea esi, [ebp + msg]
    mov eax, 2              ; sys_print_cr
    int 0x80
    ret

msg:
    db "available commands:", 13
    db "  cat       print file contents", 13
    db "  cd        change directory", 13
    db "  cp        copy file", 13
    db "  ls        list directory entries", 13
    db "  mkdir     create directory", 13
    db "  mv        move/rename file", 13
    db "  ping      simulate ping output", 13
    db "  ps        show process status", 13
    db "  pwd       print working directory", 13
    db "  rm        remove file", 13
    db "  rmdir     remove empty directory", 13
    db "  touch     create empty file", 13
    db "  vi        simple text editor", 13
    db "  write     write text to file", 13
    db "  exit      shutdown system", 13
    db "  help      show this help", 13
    db "  dump      hex dump of memory region", 13
    db "  alloc     allocate heap pages", 13
    db "  dealloc   free heap allocation", 13
    db "  poke      write byte to memory address", 13
    db "  peek      read byte from memory address", 13
    db "  calc      simple integer calculator", 13, 0
