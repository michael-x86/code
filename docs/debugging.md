# Debugging

## GDB

```
./asm -d                    # start QEMU with GDB server
gdb                         # in another terminal
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) continue
```

There are no debug symbols (flat binary). Use raw addresses:

```
0x7C00      bootloader entry
0xC0100000  kernel entry
```

### Common Breakpoints

```
(gdb) break *0x7C00        # bootloader start
(gdb) break *0xC0100000    # kernel start
(gdb) break *0xC010xxxx    # find address in kernel.lst
```

`build/kernel.lst` is the listing file and can help you locate routine offsets.

## QEMU Monitor

Add `-monitor stdio` to the QEMU invocation (or modify `build/asm` temporarily)
for the QEMU monitor. Useful commands:

```
info registers      # dump CPU state
info mem            # show mapped memory
xp /16xb 0xC0100000 # examine physical memory
```

## Troubleshooting

| Symptom                    | Likely Cause                          |
|----------------------------|---------------------------------------|
| QEMU triple fault          | Paging or GDT misconfiguration        |
| No keyboard input          | IRQ1 not enabled / IDT entry missing  |
| Screen garbage             | VGA segment register wrong            |
| Filesystem empty           | Content dirs missing or gen_fs.py err |
| Persistence not working    | ATA PIO timing / wrong drive select   |
| Userland crash             | Bad syscall args / buffer overflow    |

## Build Artifacts

```
build/os.img        final disk image
build/kernel.bin    raw kernel binary
build/kernel.lst    assembly listing (addresses + opcodes)
build/fs.inc        generated filesystem include
build/bin/          compiled userland binaries
```
