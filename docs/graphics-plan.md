# Graphics Subsystem Research & Implementation Plan

## Current State

```
Display:  VGA text mode 03h (80×25, 16 colors)
Driver:   Direct MMIO to 0xC00B8000 (higher-half mapped text buffer)
Shell:   putchar/print/newline/cls in shell.inc — character-cell only
Pixels:  NONE — no framebuffer, no pixel plotting, no sprites
```

The kernel ships `putchar`, `print`, `newline`, `cls`, `scroll`, `cursor`,
and `print_hex_drawd/print_int_decimal`. All go through `VGA_TEXT_BUFFER`
at `0xC00B8000` (physical `0xB8000`, identity-mapped + higher-half).

## What's Needed: Two Paths

There are two approaches, and they apply differently depending on what
QEMU/emulated hardware you are targeting:

### Path A — VGA Graphics Mode (Simpler, works everywhere in QEMU)

Switch the emulated VGA adapter from text mode 03h to a planar graphics
mode. The easiest mode-13h equivalent in QEMU is actually the **VESA BIOS
Extensions (VBE)** framebuffer, which QEMU's Bochs VBE emulation provides
out of the box. Options by complexity:

| Approach | Resolution | Colors | Complexity | QEMU support |
|---|---|---|---|---|
| Mode 13h (VGA) | 320×200 | 256 (palette) | Low | Yes |
| VGA Mode X | 320×240 | 256 (palette) | Medium | Yes |
| VBE 1.2 (banked) | Varies | 256/64K | Medium | Yes (Bochs) |
| VBE 2.0+ (linear) | Varies | 32-bit RGBA | Medium-High | Yes (Bochs) |

### Path B — Native GPU (VESA/VBE Linear Framebuffer)

Set a VESA mode via INT 10h in real mode during boot, or use VBE 2.0
protected-mode interface, then WRAM the linear framebuffer. QEMU supports
Bochs VBE; real hardware support varies.

**Recommendation: Path A, VBE linear framebuffer.** It works in QEMU,
produces a real linear framebuffer at a known physical address, and
provides a path to high-resolution 32-bit color. The initial boot mode
switch must be done in 16-bit real mode (bootloader stage), because INT 10h
is a BIOS call unavailable in protected mode.

---

## Detailed Implementation Phases

### Phase 1 — VBE Mode Switch in Bootloader

**What:** In `bootloader.asm`, *before* switching to protected mode, call the
VGA BIOS (INT 10h AX=4F02h) to set a VBE graphics mode.

**Why this must happen in the bootloader:**
INT 10h is a real-mode BIOS interrupt. Once the CPU enters 32-bit protected
mode (CR0.PE=1), BIOS interrupts are gone forever. The mode MUST be set
before the ` jmp CODE_SEG:protected_mode_entry `.

**Recommended mode:** `0x118` (1024×768×24-bit) or `0x115` (800×600×24-bit)
or `0x112` (640×480×24-bit). Mode `0x118` gives a nice desktop-class
resolution and is universally supported.

**What the mode switch provides:**
1. A linear framebuffer at a physical address (typically `0xFD000000` depending
   on mode — read from VBE mode info block)
2. Pixel width, height, bits-per-pitch returned in the VBE mode info block
3. The framebuffer is write-only (reading it works but is very slow on real HW)

**Additional data you must preserve for the kernel:**
After the mode switch, the kernel needs to know:
- Framebuffer physical address
- Resolution (width × height)
- Bytes per scanline (pitch)
- Bytes per pixel (BPP / 8)
- Whether the mode has a linear framebuffer bit set

Store these in a small struct in a known memory location (e.g., a fixed
address in the boot sector padding, or in registers that survive the
protected-mode transition).

**Code sketch (bootloader addition):**

```nasm
; --- Set VBE mode via BIOS INT 10h (must be in 16-bit real mode) ---
set_vbe_mode:
    ; Get VBE info first (optional — verify 4F00h/4F01h support)
    mov ax, 0x4F01        ; VBE get mode info
    mov cx, 0x0118        ; mode 1024×768×24-bit (0x4000 = linear framebuffer)
    mov di, vbe_mode_info ; 256-byte buffer in boot sector padding
    int 0x10
    cmp ax, 0x004F        ; success?
    jne .no_vbe

    ; Read framebuffer physical ptr from mode info block (offset 40)
    mov eax, [vbe_mode_info + 40]   ; physical address of linear framebuffer
    mov [vbe_fb_phys], eax
    mov ax, [vbe_mode_info + 16]    ; bytes per scanline (pitch)
    mov [vbe_pitch], ax
    mov ax, [vbe_mode_info + 18]    ; width
    mov [vbe_width], ax
    mov ax, [vbe_mode_info + 20]    ; height
    mov [vbe_height], ax
    mov al, [vbe_mode_info + 25]    ; bits per pixel
    mov [vbe_bpp], al

    ; Set the mode
    mov ax, 0x4F02
    mov bx, 0x0118        ; OR with 0x4000 for linear framebuffer
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe
    mov byte [vbe_enabled], 1
.no_vbe:
```

**Space concern:** The boot sector is 512 bytes (510 usable with boot
signature). The VBE mode info block is 256 bytes, plus ~60 bytes of code
and ~20 bytes of stored state = ~336 bytes. The current bootloader uses
approximately 350 bytes. This is very tight.

**Options:**
1. Make the bootloader a 2-sector load (1024 bytes) — easiest
2. Store the mode info block above 0x7C00 (e.g., at 0x8000) and discard it
3. Don't store the full info block — just the mode number in BX, and have
   the kernel unparse it (less flexible)
4. Use Mode 13h (INT 10h AX=13h) — no VBE info block needed since the
   framebuffer is always at 0xA0000 with known geometry 320×200×1 byte

**Best tradeoff for VEB QEMU:** Option 4 (Mode 13h) for simplest possible
start, then optionally upgrade to VBE later.

### Phase 2 — Framebuffer Mapping in Kernel Page Tables

**What:** Map the framebuffer physical address into the kernel's page tables
so the kernel can write to it via virtual addresses.

**Current layout problem:**
The existing paging setup maps 20MB (PDE 0..4) plus a higher-half mirror.
The framebuffer at e.g., `0xFD000000` (or the Mode 13h framebuffer at
`0xA0000`) is NOT in this mapping.

For **Mode 13h** (framebuffer at `0xA0000`):
- Physical `0xA0000`-`0xBFFFF` = VGA memory region
- This falls within the first 4 MB identity-mapped by `identity_page_table`
- NO new mapping needed! Just kernel_map it into higher-half

For **VBE linear framebuffer** (typically at `0xE0000000` or `0xFD000000`):
- Must allocate a new page table for the framebuffer's physical region
- Map it at a chosen virtual address (e.g., `0xC8000000` — above the heap
  region)
- Framebuffer size for 1024×768×32-bit = 3,145,728 bytes = 768 pages = 3 MB
  → needs 1 page table (maps 4 MB)

**Code sketch for higher-half Mode 13h mapping (no new page table needed):**

```nasm
; Mode 13h framebuffer at 0xA0000 is in identity_page_table (PDE 0)
; Compute higher-half virtual addr: 0xC0000000 + 0xA0000 = 0xC00A0000
VGA_FRAMEBUFFER equ 0xC00A0000   ; higher-half alias of identity-mapped VGA
```

**For VBE linear framebuffer, add to paging.inc:**

```nasm
; --- In page_mapping, add framebuffer page table ---
; framebuffer_page_table: map physical framebuffer region
; Add FBE PDE in directory:
    lea eax, [framebuffer_page_table + ebp]
    or eax, PAGE_PRESENT_RW
    ; PDE index chosen for virtual framebuffer addr, e.g., 0xC8000000 >> 22 = 768+6=774
    mov [edx + (774 * 4)], eax        ; first mapping
    mov [edx + (774 * 4)], eax        ; (same higher-half philosophy)
```

**But wait — there's a cleaner approach.** The existing `map_page` function
can map individual pages at runtime. Map framebuffer pages dynamically
as needed, or map them all at boot. For performance, pre-map all pages
at boot during `page_mapping`.

### Phase 3 — Pixel Plotting Primitives

**Add to a new `graphics.inc`:**

```nasm
; ── pixel — plot a single pixel
;   in: eax = x, ebx = y, ecx = color (GRAPHICS_BPP-dependent)
pixel:
    ; addr = framebuffer + y * pitch + x * bytes_per_pixel
    push edx
    push edi
    mov edi, [vfb_pitch]
    imul edi, ebx            ; y * pitch
    mov edx, eax
    imul edx, [vfb_bytes_per_pixel]
    add edi, edx             ; + x * bpp
    add edi, [vfb_addr]      ; + framebuffer base
    ; Write color (1, 2, or 3 bytes depending on BPP)
    mov al, cl
    cmp byte [vfb_bpp], 8
    je .write_8
    cmp byte [vfb_bpp], 24
    je .write_24
    cmp byte [vfb_bpp], 32
    je .write_32
.write_8:
    mov [edi], al
    jmp .done
.write_24:
    mov [edi], cl
    shr ecx, 8
    mov [edi+1], cl
    shr ecx, 8
    mov [edi+2], cl
    jmp .done
.write_32:
    mov [edi], ecx
.done:
    pop edi
    pop edx
    ret
```

**Then build higher-level primitives on top:**
- `fill_rect` — filled rectangle (for backgrounds, bars, windows)
- `draw_hline` / `draw_vline`
- `draw_rect` — outline rectangle
- `draw_char` — bitmap font render at pixel position
- `draw_string` — string at pixel position
- `draw_line` — Bresenham's algorithm
- `blit_rect` — copy rectangle (for window scroll, dirty-region redraw)

### Phase 4 — Bitmap Font

To render text in graphics mode, you need a bitmap font. Options:

1. **Embde a 8×16 or 8×8 bitmap font** in the kernel binary. A complete
   256-character 8×8 font is 256 × 8 = 2048 bytes. For 8×16 = 4096 bytes.
   This is small.

2. **VGA font extraction:** In real mode, the BIOS stores the VGA font at
   `0xF000:FA6E` (8×16 font) for mode 03h. This is not accessible once in
   protected mode, so it must be copied during boot.

3. **Hardcode a minimal font** — just printable ASCII (95 chars × 8×16 =
   1520 bytes) or even just a tiny 6×8 font.

**Recommended:** Option 1. Embed a compact 8×16 bitmap font as a `.inc`
binary blob. Many open-source bitmap fonts exist (IBM VGA, Terminus, etc.).

**Font rendering function:**

```nasm
; ── draw_char — render a glyph at pixel coords
;   in: al = ASCII char, ebx = x, ecx = y, edx = color
draw_char:
    push esi
    push edi
    push eax
    ; glyph = font_base + (char * FONT_HEIGHT)
    xor eax, eax
    pop eax
    push eax
    movzx eax, al
    imul eax, FONT_HEIGHT        ; each glyph is FONT_HEIGHT bytes
    add eax, font_base           ;esi = glyph data pointer
    mov esi, eax
    ; Compute top-left pixel address
    mov edi, ecx
    imul edi, [vfb_pitch]       ; y * pitch
    shl ebx, 3                   ; x * 8 (FONT_WIDTH)
    mov edx, ebx
    imul edx, [vfb_bytes_per_pixel]
    add edi, edx
    add edi, [vfb_addr]         ; edi = pixel address

    mov ecx, FONT_HEIGHT        ; row counter
.row:
    push ecx
    lodsb                        ; al = font row (8 bits)
    mov cx, FONT_WIDTH           ; 8
    mov ebx, edi                 ; save row start
.bit:
    rol al, 1
    jnc .skip
    ; draw pixel at ebx
    push eax
    push ecx
    push edx
    push edi
    mov ecx, edx                  ; color
    ; (x offset = 8-cx) — compute pixel address
    mov edx, 8
    sub edx, cx                   ; x offset within glyph
    imul edx, [vfb_bytes_per_pixel]
    push dword ebx
    add ebx, edx
    mov edi, ebx
    pop dword ebx
    call pixel_direct            ; pixel at edi, color ecx
    pop edi
    pop edx
    pop ecx
    pop eax
.skip:
    add edi, [vfb_bytes_per_pixel]
    dec cx
    jnz .bit
    mov edi, ebx                  ; restore row start
    add edi, [vfb_pitch]        ; next scanline
    pop ecx
    loop .row
    pop eax
    pop edi
    pop esi
    ret
```

### Phase 5 — Backbuffer (Double Buffering)

Writing directly to the visible framebuffer causes visible flicker because
the display refresh reads from the same memory.

**Solution:** Allocate a backbuffer in kernel heap memory:

```nasm
; Backbuffer = malloc(width * height * bytes_per_pixel)
; Each frame:
;   1. Draw everything to the backbuffer (invisible)
;   2. memcpy backbuffer → visible framebuffer (single fast copy)
; Result: smooth, tear-free updates
```

**Backbuffer size:**
- 320×200×1 byte (Mode 13h) = 64 KB → trivial
- 640×480×3 bytes (VBE 24-bit) = 921 KB → within heap
- 800×600×4 bytes (VBE 32-bit) = 1.8 MB → within heap
- 1024×768×4 bytes = 3 MB → within heap (12 MB available)

The heap has 12 MB (3 page tables × 4 MB), so any of these fit. Use
`alloc_pages` from `memory.inc`.

For Mode 13h, a 64 KB backbuffer can go in the existing heap.

### Phase 6 — Input Abstraction for Graphics

**Problem:** Existing keyboard input (`sys_get_key` → `kbd_buf` → scancode)
works fine, but the new graphics system may also want mouse input.

**PS/2 Mouse:**
1. Enable AUX (mouse) via PS/2 controller port 0x64/0x60
2. Enable IRQ12 (mouse interrupt)
3. Install an IDT handler for IRQ12 (interrupt 0x2C after PIC remap)
4. Parse the 3-byte PS/2 mouse packet: buttons, delta-X, delta-Y
5. Track cursor position, deliver events to the graphics layer

**QEMU mouse:** Works seamlessly once the PS/2 mouse is initialized.

**This is optional for Phase 1–5.** Keyboard input can drive a
text cursor rendered in graphics mode until mouse support is added.

### Phase 7 — Graphics Syscalls

Expose graphics to userland programs via `int 0x80`:

| Syscall | Number | Description |
|---|---|---|
| sys_fb_info | 31 | Get framebuffer address, w, h, pitch, bpp |
| sys_fb_swap | 32 | Swap/display the backbuffer |
| sys_fb_plotpixel | 33 | Plot a single pixel |
| sys_fb_fillrect | 34 | Fill a rectangle with color |
| sys_fb_drawline | 35 | Draw a line (Bresenham) |

This lets userspace programs draw directly.

### Phase 8 — Window Manager (Conceptual, Later)

Once basic framebuffer + rendering + mouse are working:
- Assign a virtual display grid
- Implement dirty-rectangle tracking
- Add a compositor that assembles window bitmaps
- sys_window_create, sys_window_move, sys_window_blit

This is a complete desktops later effort. The first 4 phases get you
a functioning graphical framebuffer.

---

## Memory Impact Summary

| Addition | Size | Location |
|---|---|---|
| graphics.inc code | ~2-3 KB | kernel code (text) |
| 8×16 bitmap font (256 chars) | 4 KB | kernel data (rodata) |
| VBE variables from bootloader | ~30 bytes | fixed addr / registers |
| Framebuffer page table | 4 KB | BSS (page-aligned) |
| Backbuffer (Mode 13h) | 64 KB | heap allocation |
| Backbuffer (VBE 800×600×32) | 1.9 MB | heap allocation |
| Backbuffer (VBE 1024×768×32) | 3 MB | heap allocation |
| PS/2 mouse buf/state | ~64 bytes | BSS |

**Conclusion:** Even the largest framebuffer (1024×768×32) only needs 3 MB
out of the 12 MB heap, leaving 9 MB for everything else. The kernel binary
grows by ~10 KB total. All very manageable.

---

## Recommended Phase Order

1. **Phase 1**: Mode 13h switch in bootloader (320×200×8)
   - Smallest bootloader modification
   - Framebuffer at 0xA0000 — already identity-mapped
   - No page table changes needed
   - Get pixels on screen in 1-2 hours

2. **Phase 2**: Map framebuffer at `0xC00A0000` in paging
3. **Phase 3**: `pixel`, `fill_rect`, `draw_char` primitives
4. **Phase 4**: Bitmap font
5. **Phase 5**: Backbuffer for clean rendering
6. **Phase 6**: Bootloader → VBE mode 0x118 (1024×768×24)
   - More bootloader space needed (2-sector load)
   - New page table for framebuffer
   - Same drawing code works
7. **Phase 7**: Graphics syscalls
8. **Phase 8**: PS/2 mouse input
