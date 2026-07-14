# Makefile for the x86 hobby OS (replaces the old ./asm script).
#
#   make            build os.img
#   make run        build + run in QEMU (windowed)
#   make fullscreen build + run fullscreen
#   make debug      build + run with a GDB server on localhost:1234 (halted)
#   make clean      remove all build artifacts
#
# Build pipeline: user programs -> embedded FS (fs.inc) -> kernel ->
# patched bootloader -> os.img (bootloader + kernel, padded, FS region preserved).

NASM   := nasm
PYTHON := python3
QEMU   := qemu-system-i386

IMG        := os.img
KERNEL     := kernel.asm
BOOTLOADER := bootloader.asm

# User programs compiled from commands/<name>.asm into bin/<name>.
COMMANDS := plot up memdump regdump stackdump bin2hex bounce dydx \
            ps kill poke peek pwd ls cd cat touch write help cls \
            verify exit rm rmdir mkdir cp mv alloc dealloc heap \
            frequency epoch banner mode13 setpixel clearpixel clear

BINS     := $(addprefix bin/,$(COMMANDS))

MAX_PROG := 4096          # hard size cap per user program (bytes)

# Embedded-FS persistence region — MUST match gen_fs.py and kernel.asm.
FS_BASE_LBA         := 512
FS_SPARE_COUNT      := 32
FS_SECTORS_PER_SLOT := 5
FS_REGION_SECTORS   := $(shell expr $(FS_SPARE_COUNT) \* $(FS_SECTORS_PER_SLOT))
IMG_SECTORS         := $(shell expr $(FS_BASE_LBA) + $(FS_REGION_SECTORS))
IMG_SIZE            := $(shell expr $(IMG_SECTORS) \* 512)

FS_STAMP := .fsfiles      # marker for the generated /proc, /var/log, /etc content

QEMU_ARGS := -drive format=raw,file=$(IMG),index=0,if=ide \
             -device isa-debug-exit,iobase=0xf4,iosize=0x04 -m 128

.PHONY: all run fullscreen debug clean
.DELETE_ON_ERROR:

all: $(IMG)

# ---- user programs (each capped at MAX_PROG bytes) ----
bin:
	@mkdir -p bin

bin/%: commands/%.asm | bin
	@$(NASM) -f bin $< -o $@
	@sz=$$(stat -c%s $@); \
	if [ $$sz -gt $(MAX_PROG) ]; then \
	    echo "ERROR: $< compiled to $$sz bytes (max $(MAX_PROG))" >&2; \
	    rm -f $@; exit 1; \
	fi; \
	echo "  + $@ ($$sz bytes)"

# ---- host-side files embedded into /proc, /var/log, /etc ----
$(FS_STAMP): Makefile
	@mkdir -p etc proc var/log
	@printf '%s\n' \
	    "root:x:0:0:root:/root:/bin/bash" \
	    "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin" \
	    "bin:x:2:2:bin:/bin:/usr/sbin/nologin" > etc/passwd
	@printf '%s\n' \
	    "processor      : 0" \
	    "vendor_id      : Cyberdyne Systems" \
	    "cpu family     : Neural-Net Processor" \
	    "model          : T-800 Series 101" \
	    "model family   : Skynet" \
	    "stepping       : Version 2.4" \
	    "flags          : learning infiltration phased-plasma" > proc/cpuinfo
	@printf '%s\n' \
	    "MemTotal:         33554432 kB" \
	    "MemFree:           8388608 kB" \
	    "MemAvailable:     16777216 kB" \
	    "Buffers:           1048576 kB" \
	    "Cached:            4194304 kB" \
	    "SwapCached:              0 kB" \
	    "" \
	    "NeuralNetCache:    2097152 kB" \
	    "TargetProfiles:     524288 kB" \
	    "SkynetReserved:    8388608 kB" \
	    "ThreatAnalysis:      realtime" \
	    "" \
	    "KernelPanic:               no" \
	    "JudgmentDay:       inevitable" > proc/meminfo
	@printf '%s\n' \
	    "2026-05-15T18:30:20 SunOS Process 'windows' [666] dumped core: signal 11" > var/log/kern.log
	@printf '%s\n' \
	    "2002-10-06T23:58:57 Solaris7 I'm sorry Dave, I can't do that..." > var/log/messages
	@printf '%s\n' \
	    "2026-05-18T19:41:00 SunOS SysV[1]: Starting There-can-be-only-one... " \
	    "2026-05-18T19:41:10 SunOS /usr/highlander: [session uid=400 pid=1]" \
	    "2026-05-18T19:41:20 SunOS Macleod-[1592]: [SysV] Successfully activated" > var/log/syslog
	@touch $@

# ---- embedded filesystem (re-scanned whenever a program or host file changes) ----
fs.inc: gen_fs.py $(BINS) $(FS_STAMP)
	@echo "[fs] scanning bin/ proc/ var/log/ etc/ ..."
	@$(PYTHON) gen_fs.py . fs.inc >/dev/null

# ---- kernel (incbin's fs.inc) ----
kernel.bin: $(KERNEL) fs.inc
	@echo "[kernel] assembling..."
	@$(NASM) -f bin $(KERNEL) -o $@
	@sz=$$(stat -c%s $@); echo "  kernel.bin: $$sz bytes ($$(( ($$sz + 511) / 512 )) sectors)"

# ---- bootloader (its KERNEL_SECTORS constant is patched from the kernel size) ----
bootloader.bin: $(BOOTLOADER) kernel.bin
	@ks=$$(( ($$(stat -c%s kernel.bin) + 511) / 512 )); \
	if [ $$(( ks + 1 )) -ge $(FS_BASE_LBA) ]; then \
	    echo "ERROR: kernel ($$ks sectors) overlaps FS region (LBA $(FS_BASE_LBA)+)" >&2; exit 1; \
	fi; \
	sed -i "s/^KERNEL_SECTORS[[:space:]]\+equ.*/KERNEL_SECTORS equ $$ks/" $(BOOTLOADER); \
	$(NASM) -f bin $(BOOTLOADER) -o $@; \
	echo "[boot] patched KERNEL_SECTORS=$$ks"

# ---- disk image: bootloader + kernel, padded to the FS region end.
#      Any existing persisted FS region is preserved across rebuilds. ----
$(IMG): bootloader.bin kernel.bin
	@backup=""; \
	if [ -f $(IMG) ] && [ $$(stat -c%s $(IMG)) -ge $(IMG_SIZE) ]; then \
	    backup=$$(mktemp); \
	    dd if=$(IMG) of=$$backup bs=512 skip=$(FS_BASE_LBA) count=$(FS_REGION_SECTORS) status=none; \
	fi; \
	cat bootloader.bin kernel.bin > $(IMG); \
	truncate -s $(IMG_SIZE) $(IMG); \
	if [ -n "$$backup" ]; then \
	    dd if=$$backup of=$(IMG) bs=512 seek=$(FS_BASE_LBA) count=$(FS_REGION_SECTORS) conv=notrunc status=none; \
	    rm -f $$backup; \
	fi; \
	echo "Build complete: $(IMG) ($$(stat -c%s $(IMG)) bytes)"

# ---- run targets ----
run: $(IMG)
	$(QEMU) $(QEMU_ARGS)

fullscreen: $(IMG)
	$(QEMU) $(QEMU_ARGS) --full-screen

debug: $(IMG)
	$(QEMU) $(QEMU_ARGS) -gdb tcp::1234 -S

clean:
	rm -f $(IMG) kernel.bin bootloader.bin fs.inc $(FS_STAMP)
	rm -f bin/*
	rm -rf proc var etc
