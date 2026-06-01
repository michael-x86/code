# Bootsector Expansion Analysis

## Current Layout (512 bytes total)

```
Offset  Size   Content
0x0000  282    Code + data (bootloader.asm, ends at line 156)
0x011A  228    Padding (times 510 - ($-$$) db 0)
0x01FE  2      Boot signature 0xAA55
```

**Free space: 228 bytes** — not enough for VBE mode info block (256 bytes)
plus setup code (~50 bytes) plus state storage (~30 bytes) = ~336 bytes total.

## Expansion Options

### Option 1: Multi-Sector Bootloader (Recommended)

Load 2-4 sectors instead of 1. The BIOS loads sector 0 (LBA 0) into
0x7C00 automatically. The bootloader then loads N-1 additional sectors
itself before entering protected mode.

**Changes needed:**
1. Increase KERNEL_SECTORS to include the extra boot sectors, OR
2. Add a separate BOOT_SECTORS equ and load them in the bootloader
3. Update the build script to pad bootloader.bin to N*512 bytes
4. Update the kernel LBA calculation: kernel starts at LBA = BOOT_SECTORS

**With 2 sectors (1024 bytes):**
- 1022 bytes available for code+data (minus 2 for signature)
- 282 bytes currently used
- **740 bytes free** — plenty for VBE

**With 4 sectors (2048 bytes):**
- Even more room, but diminishing returns

**Recommended: 2 sectors.** This gives 740 bytes of free space, which is
more than enough for VBE setup code + mode info block + state.

### Option 2: Relocate the Mode Info Block

Don't store the 256-byte VBE mode info block in the boot sector at all.
Instead, store it at a known memory address outside the boot sector:
- 0x8000 (right after the boot sector, before kernel load at 0x10000)
- 0x500 (the scratch area already used for FS LBA)
- Anywhere in the 228-byte free padding + below the stack at 0x9000

Then the bootloader only needs ~80 bytes of code + data in the boot
sector itself, which fits in the existing 228 bytes.

**Tradeoff:** Slightly more complex, but avoids changing the sector layout.

### Option 3: Use Mode 13h Instead of VBE

Mode 13h (INT 10h AH=13h) needs only 3 bytes of code:
    mov ax, 0x0013
    int 0x10

No mode info block needed — the framebuffer is always at 0xA0000,
always 320×200×8-bit. This fits in the existing boot sector with room
to spare.

**Tradeoff:** Limited to 320×200×256 colors. No high-resolution support.

## Recommendation

**Option 1 (2-sector bootloader) + VBE.** It's the cleanest approach:
- Minimal code changes
- Full VBE support (high resolution, 24/32-bit color)
- The build script already handles variable KERNEL_SECTORS
- Only the bootloader itself needs to load the extra sector

## Implementation Plan for 2-Sector VBE Bootloader

### Step 1: Add BOOT_SECTORS constant

In bootloader.asm:
    BOOT_SECTORS equ 2       ; 1 boot sector + 1 extension sector

### Step 2: Load the extension sector

In the bootloader, after A20 enable, before kernel load:
    ; Load extension sector(s) from LBA 1 into 0x7E00 (right after boot sector)
    mov si, dap_ext
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 13h

    ; Update DAP for kernel load (kernel now starts at LBA = BOOT_SECTORS)
    ; dap.kernel_lba = BOOT_SECTORS

### Step 3: Add VBE setup code in the extension sector

The extension sector (loaded at 0x7E00) contains:
- VBE mode info block buffer (256 bytes at 0x7E00)
- VBE setup code (~60 bytes)
- VBE state storage (~20 bytes)

### Step 4: Update build script

In build/asm:
    BOOT_SECTORS=2
    # Pad bootloader.bin to BOOT_SECTORS * 512
    # Kernel LBA = BOOT_SECTORS (instead of 1)
    # Update dap.kernel_lba accordingly

### Step 5: Pass VBE state to kernel

Store VBE parameters at a known physical address (e.g., 0x500-0x520):
    vbe_fb_phys    dd 0    ; framebuffer physical address
    vbe_width      dw 0    ; width in pixels
    vbe_height     dw 0    ; height in pixels
    vbe_pitch      dw 0    ; bytes per scanline
    vbe_bpp        db 0    ; bits per pixel
    vbe_enabled    db 0    ; 1 = VBE mode set successfully

The kernel reads these at startup to find the framebuffer.
