# Running on Real Hardware

The kernel currently targets a clean QEMU environment with standard legacy x86
devices. The CPU instruction set is identical on real hardware, so this is not a
rewrite — it is a set of hardcoded emulator assumptions that need to become
runtime detection plus fallbacks. This document catalogs every known assumption,
ordered by how badly it breaks on bare metal, as a reference for future work.

## Critical blockers (won't boot or won't function)

### 1. No memory detection

- **Where:** `kernel/src/includes/constants.inc` hardcodes the layout
  (identity map, kernel region, a fixed 12 MB heap at `0x00800000–0x01400000`,
  higher-half mirror at `0xC0000000+`). The QEMU memory size is set in
  `build/asm` (`-m 128`).
- **Problem:** There is no `INT 15h, AX=E820h` call anywhere. On hardware with a
  different memory map (reserved regions, MMIO holes, more/less RAM) the fixed
  heap can land on unusable physical memory, causing page faults, or extra RAM
  is silently wasted.
- **Fix:** Call E820 in the real-mode bootloader, build a memory map, pass it to
  the kernel via the handoff area, and size the heap / frame bitmap dynamically.

### 2. Shutdown is QEMU-only

- **Where:** `kernel/src/includes/vga.inc` writes to port `0xF4`
  (QEMU `isa-debug-exit`, configured in `build/asm`).
- **Problem:** On real hardware port `0xF4` is a no-op; the machine just hangs.
- **Fix:** Implement ACPI shutdown (find the FADT, write to PM1a_CNT), and a
  keyboard-controller reset (or triple-fault) for reboot.

### 3. UEFI machines have no legacy VGA

- **Where:** The display layer assumes the `0xB8000` text buffer and the
  `0xA0000` mode-13h buffer, programming VGA registers directly via port I/O
  (`kernel/src/includes/vga.inc`, `kernel/src/includes/graphics.inc`). No BIOS
  `INT 10h` is used.
- **Problem:** UEFI-only systems do not expose legacy VGA memory or I/O.
- **Fix (lowest effort):** Boot via legacy BIOS / CSM so VGA stays available.
  **Fix (proper):** A UEFI bootloader that obtains a GOP linear framebuffer and
  passes its base/pitch/format to the kernel, then a framebuffer-based text and
  graphics path instead of `0xB8000`/`0xA0000`.

## Likely to break (machine dependent)

### 4. Disk assumes legacy IDE on a fixed port

- **Where:** `kernel/src/includes/ata.inc` uses ATA PIO at hardcoded port
  `0x1F0`. The bootloader (`kernel/bootloader.asm`) assumes the filesystem is on
  the **primary slave** drive and stores the port/drive selector at physical
  `0x500` for the kernel.
- **Problem:** Real machines use SATA (AHCI) or NVMe. SATA frequently works in
  BIOS "IDE / legacy" compatibility mode, but the hardcoded two-disk QEMU layout
  (boot image = master, FS image = slave) rarely matches real systems. PIO only
  (no DMA), LBA28 only (137 GB cap), single controller (no `0x170` secondary).
- **Fix:** Enumerate ATA drives and detect which one holds the FS; longer term,
  add an AHCI driver. Boot from a single device where possible.

### 5. Input is PS/2 only

- **Where:** IRQ1 keyboard at port `0x60`/`0x64`
  (`kernel/src/includes/input.inc`, `kernel/src/includes/irq.inc`); IRQ12 PS/2
  mouse (`kernel/src/includes/graphics.inc`). Scancode set 1 assumed.
- **Problem:** USB-only laptops and motherboards have no PS/2 controller.
- **Fix (lowest effort):** Rely on BIOS "legacy USB emulation" (PS/2 emulation
  for USB HID), and test on a machine with a real PS/2 port. **Fix (proper):** a
  USB host-controller + HID stack.

### 6. A20 enabled via port 0x92 only

- **Where:** `kernel/bootloader.asm` uses the fast-A20 gate (port `0x92`)
  without verifying success or falling back.
- **Problem:** Usually fine; occasionally the fast gate is absent or behaves
  differently.
- **Fix:** Test whether A20 is already enabled, and fall back to the
  keyboard-controller method (`0x64`/`0x60`) if the fast gate fails.

## Minor / probably fine

### 7. PIC + PIT legacy timers

- **Where:** 8259 PIC remap and 100 Hz PIT (divisor `11932` in
  `kernel/src/includes/constants.inc`) in `kernel/src/includes/pic.inc`.
- **Status:** Universally supported in legacy mode. No APIC/HPET support, so no
  SMP and no high-precision timing, but it boots. The scheduler and the shell's
  live tick counter depend on the exact 100 Hz rate; small drift is possible but
  harmless.

### 8. Kernel load via INT 13h

- **Where:** `kernel/bootloader.asm` loads the kernel with `INT 13h` using the
  LBA extension (AH=42h) and a CHS fallback (AH=02h). `KERNEL_SECTORS` is
  patched per build by `build/asm`.
- **Status:** Standard BIOS, portable. Fine on any legacy-BIOS machine.

## Suggested path to a first real boot

1. Target **legacy BIOS boot** (or enable CSM) to avoid the UEFI/GOP rewrite.
2. Add **E820 memory detection** in the bootloader + dynamic heap sizing.
3. Replace the **port-0xF4 shutdown** with ACPI shutdown / keyboard reset.
4. Boot from a **USB stick in BIOS legacy mode** and add minimal **ATA drive
   enumeration** instead of assuming primary-slave.
5. Ensure **BIOS legacy USB keyboard emulation** is enabled, or test on a
   machine with a PS/2 port.

That covers most pre-UEFI-only x86 boxes. Full modern-hardware support (UEFI
GOP, AHCI/NVMe, USB HID, APIC/SMP) is a substantially larger effort and is
effectively a set of new drivers.

## Summary table

| # | Assumption | Severity | Fix |
|---|------------|----------|-----|
| 1 | No memory detection (no E820) | Critical | Detect RAM, pass map to kernel, size heap dynamically |
| 2 | Shutdown via QEMU port 0xF4 | Critical | ACPI shutdown + keyboard/triple-fault reset |
| 3 | Legacy VGA `0xB8000`/`0xA0000` | Critical on UEFI | Legacy/CSM boot, or UEFI GOP framebuffer |
| 4 | Fixed ATA port 0x1F0, primary-slave FS | High | Enumerate drives; later AHCI/NVMe |
| 5 | PS/2-only keyboard/mouse | Medium | BIOS legacy USB emulation, or USB HID stack |
| 6 | A20 via port 0x92 only | Low | Verify + keyboard-controller fallback |
| 7 | PIC/PIT legacy timers, no APIC | Low | Fine for now; APIC/HPET later for SMP |
| 8 | Kernel load via INT 13h | None | Already portable |
