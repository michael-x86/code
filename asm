#!/bin/bash
set -e

BOOT_ASM="bootloader.asm"
KERNEL_ASM="kernel.asm"
IMG="os.img"

# Programs to compile
COMMANDS=(plot up memdump bounce cls exit help ps vi
          poke peek pwd ls cd cat touch write verify
          rm rmdir mkdir cp mv bin2hex alloc dealloc)

RUN_QEMU=false
FULLSCREEN=false
DEBUG=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--run)        RUN_QEMU=true; shift;;
        -f|--fullscreen) FULLSCREEN=true; RUN_QEMU=true; shift;;
        -d|--debug)      DEBUG=true; RUN_QEMU=true; shift;;
        -h|--help)
            cat <<EOF
Usage: ./asm [options]
  -r, --run         Compile and run in QEMU
  -f, --fullscreen  Run in fullscreen
  -d, --debug       Run with GDB on localhost:1234
  -h, --help        Show this help
EOF
            exit 0;;
        *) echo "Unknown option: $1"; exit 1;;
    esac
done

echo "rm -f bin/*"
rm -f  bin/*

mkdir -p etc bin proc var/log
echo "root:x:0:0:root:/root:/bin/bash" > etc/passwd
echo "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin" >> etc/passwd
echo "bin:x:2:2:bin:/bin:/usr/sbin/nologin" >> etc/passwd

echo "processor      : 0" > proc/cpuinfo
echo "vendor_id      : Cyberdyne Systems" >> proc/cpuinfo
echo "cpu family     : Neural-Net Processor" >> proc/cpuinfo
echo "model          : T-800 Series 101" >> proc/cpuinfo
echo "model family   : Skynet" >> proc/cpuinfo
echo "stepping       : Version 2.4" >> proc/cpuinfo
echo "flags          : learning infiltration phased-plasma" >> proc/cpuinfo

echo "MemTotal:         33554432 kB
MemFree:           8388608 kB
MemAvailable:     16777216 kB
Buffers:           1048576 kB
Cached:            4194304 kB
SwapCached:              0 kB

NeuralNetCache:    2097152 kB
TargetProfiles:     524288 kB
SkynetReserved:    8388608 kB
ThreatAnalysis:      realtime

KernelPanic:               no
JudgmentDay:       inevitable" > proc/meminfo

echo "2026-05-15T18:30:20 SunOS Process 'windows' [666] dumped core: signal 11" > var/log/kern.log
echo "2002-10-06T23:58:57 Solaris7 I'm sorry Dave, I can't do that..." > var/log/messages
echo "2026-05-18T19:41:00 SunOS SysV[1]: Starting There-can-be-only-one... 
2026-05-18T19:41:10 SunOS /usr/highlander: [session uid=400 pid=1]
2026-05-18T19:41:20 SunOS Macleod-[1592]: [SysV] Successfully activated" > var/log/syslog
echo "[1/6] Building commands..."
for p in "${COMMANDS[@]}"; do
    cntr=$((cntr + 1))
    src="./commands/${p}.asm"
    #echo "$src"
    out="bin/${p}"
    if [[ ! -f "$src" ]]; then
        echo "  ! missing $src — skipping"
        continue
    fi
    nasm -f bin "$src" -o "$out"
    sz=$(stat -c%s "$out")
    if (( sz > 4096 )); then
        echo "ERROR: $src compiled to $sz bytes (max 4096)" >&2
        exit 1
    fi
    echo "  + bin/$p ($sz bytes)"
done

if [ "${#COMMANDS[@]}" -ne "$cntr" ]; then
  echo "Missing commands"
fi

echo "[2/6] Scanning filesystem (bin/ proc/ var/log/ ..."
python3 gen_fs.py . fs.inc | sed 's/^/  /'

echo "[3/6] Assembling kernel..."
nasm -f bin "$KERNEL_ASM" -o kernel.bin

KERNEL_SIZE=$(stat -c%s kernel.bin)
KERNEL_SECTORS=$(((KERNEL_SIZE + 511) / 512))
KERNEL_BYTES=$((KERNEL_SECTORS * 512))
echo "[4/6] Kernel size: $KERNEL_SIZE bytes ($KERNEL_SECTORS sectors)"
sed -i "s/^KERNEL_SECTORS.*/KERNEL_SECTORS  equ 0x$(printf '%02X' $KERNEL_SECTORS)/" bootloader.asm

echo "[5/6] Assembling bootloader..."
nasm -f bin "$BOOT_ASM" -o bootloader.bin

echo "[6/6] Linking os.img..."
FS_BASE_LBA=256
FS_SPARE_COUNT=16
FS_SECTORS_PER_SLOT=3
FS_BYTES=$((FS_SPARE_COUNT * FS_SECTORS_PER_SLOT * 512))
FS_OFFSET=$((FS_BASE_LBA * 512))
TARGET=$((FS_OFFSET + FS_BYTES))
if (( KERNEL_SECTORS + 1 >= FS_BASE_LBA )); then
    echo "ERROR: kernel ($KERNEL_SECTORS sectors) overlaps FS region (LBA $FS_BASE_LBA+)" >&2
    exit 1
fi
# Preserve any existing FS region so persisted files survive rebuilds.
FS_BACKUP=""
if [[ -f "$IMG" ]] && (( $(stat -c%s "$IMG") >= TARGET )); then
    FS_BACKUP=$(mktemp)
    dd if="$IMG" of="$FS_BACKUP" bs=512 skip=$FS_BASE_LBA \
       count=$((FS_SPARE_COUNT * FS_SECTORS_PER_SLOT)) status=none
fi
cat bootloader.bin kernel.bin > "$IMG"
truncate -s "$TARGET" "$IMG"
if [[ -n "$FS_BACKUP" ]]; then
    dd if="$FS_BACKUP" of="$IMG" bs=512 seek=$FS_BASE_LBA \
       count=$((FS_SPARE_COUNT * FS_SECTORS_PER_SLOT)) conv=notrunc status=none
    rm -f "$FS_BACKUP"
fi

echo ""
echo "Build complete: $IMG ($(stat -c%s "$IMG") bytes)"
echo ""
echo "KERNEL_SECTORS equ $KERNEL_SECTORS -> bootloader.asm"
echo ""
rm -f kernel.bin bootloader.bin
rm -f  fs.inc
rm -rf var proc etc
if [[ "$RUN_QEMU" == true ]]; then
    QEMU_ARGS=(
        -drive format=raw,file="$IMG",index=0,if=ide
        -device isa-debug-exit,iobase=0xf4,iosize=0x04
        -m 128
    )

    if [[ "$FULLSCREEN" == true ]]; then
        QEMU_ARGS+=(--full-screen)
        echo "Mode: FULLSCREEN (Ctrl+Alt+F to exit)"
    else
        echo "Mode: WINDOWED"
    fi

    if [[ "$DEBUG" == true ]]; then
        QEMU_ARGS+=(-gdb tcp::1234 -S)
        echo "Debug: GDB on localhost:1234"
    fi
    echo ""
    qemu-system-i386 "${QEMU_ARGS[@]}"
else
    echo "To run: ./asm -r    (fullscreen: -f    gdb: -d)"
fi
