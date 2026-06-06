# Plan: Port x86 OS Kernel to Real Hardware

## Goal
Bring the existing in-development x86 OS kernel (currently tested exclusively in QEMU) to run on physical x86 hardware, enabling boot from removable media and stable operation with real hardware components.

## Current Context
- Project state: Scratch-built x86 OS with bootloader, paging, VFS, syscalls, shell, and userland assembly. Graphics subsystem (VBE framebuffer, pixel plotting, backbuffer) is in progress.
- Current testing: Entirely in QEMU emulator.
- Known kernel quirks: `map_page` (paging.inc) saves/restores EDI; `userland.inc` restricted to macros only to avoid stray code before `_start`.
- Assumed current boot flow: BIOS-based MBR bootloader loading kernel into memory.

## Assumptions
1. You have access to a test x86 machine (old PC, thin client, or x86 SBC) with BIOS firmware.
2. You can write bootable media (USB drive, CD) and attach a serial console (or USB-to-serial adapter) for debugging.
3. Existing code uses standard x86 BIOS interfaces (VBE, ATA, PS/2) rather than QEMU-specific extensions.

## Proposed Approach
Audit emulator-specific assumptions, then add real hardware support layer-by-layer: boot media, storage, display, input, interrupt handling, and memory mapping. Test incrementally with serial output to debug issues before display drivers are stable.

## Step-by-Step Plan
1. **Codebase Audit for QEMU-Specific Hacks**
   - Scan for hardcoded memory addresses, fixed device assumptions, or QEMU-only emulated hardware references (e.g., VirtIO, QEMU-specific VBE modes).
   - Verify `E820` memory map parsing is dynamic (not hardcoded to QEMU's default 512MB/1GB).
   - Check `map_page` and `free_pages` for EDI-related bugs noted in memory.

2. **Bootable Media Pipeline**
   - Update Makefile to generate a raw disk image compatible with USB/HDD boot (MBR + kernel + optional initrd).
   - Test image in QEMU with `-hda /dev/sdX` (or equivalent) to confirm boot before writing to physical media.

3. **Real Storage Driver**
   - Replace QEMU's emulated IDE/ATA layer with a generic ATA/ATAPI driver supporting physical disks.
   - Add fallback to CHS (Cylinder-Head-Sector) addressing for older hardware if LBA fails.

4. **Display Subsystem Hardening**
   - Validate VBE 2.0+ compatibility: Add detection for VBE support and fallback to VGA text mode if VBE initialization fails.
   - Test framebuffer mapping on real hardware (VBE modes may vary between BIOS implementations).

5. **Input Driver Implementation**
   - Add PS/2 keyboard driver (start with PS/2 before USB, as USB stack is more complex).
   - Optional: Add PS/2 mouse driver for future GUI support.

6. **Interrupt Controller Verification**
   - Confirm 8259A PIC configuration works on real hardware (QEMU emulates this, but physical hardware may have quirks).
   - Test timer and keyboard interrupts via serial logging.

7. **Incremental Real Hardware Testing**
   - Write bootable USB image using `dd` or similar tool.
   - Boot test machine with serial console attached (log output to catch early boot failures).
   - Debug layer-by-layer: serial output first, then VGA text, then VBE graphics.

## Files Likely to Change
- `bootloader/MBR.asm` (boot media compatibility)
- `paging.inc`, `memory.inc` (memory mapping, EDI bug fix)
- `vbe.inc` (VBE fallback, real hardware mode support)
- `storage.inc` (ATA driver)
- `keyboard.inc` (PS/2 driver)
- `interrupt.inc` (PIC configuration)
- `Makefile` (bootable image generation)

## Validation
- Each step validated first in QEMU, then on real hardware.
- Serial console logs compared against QEMU output to identify discrepancies.
- Graphics output verified on real display once VBE is stable.

## Risks, Tradeoffs, and Open Questions
- **Risk**: Varying BIOS/VBE implementations across hardware may cause display or boot failures. *Mitigation*: Add VBE detection and fallback to VGA text mode.
- **Risk**: Modern laptops lack serial ports; USB-to-serial adapters may introduce latency. *Mitigation*: Use a desktop or thin client with native serial port first.
- **Tradeoff**: PS/2 input is simpler than USB but limits hardware support. *Decision*: Prioritize PS/2 for initial port, add USB later.
- **Open Question**: Does the test machine support USB boot from BIOS? (May require enabling in BIOS settings.)
