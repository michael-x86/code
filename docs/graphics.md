# Graphics

The kernel provides an OS-level graphics capability built on **VGA Mode 13h**
(320×200, 256 colors). It is not a window system — it gives userland programs
(games, graphical TUIs, visualizers) a framebuffer and a small set of drawing
primitives, plus a PS/2 mouse. The text-mode shell is preserved: a program
switches into graphics mode on demand and switches back when it exits.

Implementation: [kernel/src/includes/graphics.inc](../kernel/src/includes/graphics.inc).
Syscall numbers and constants: [kernel/src/includes/constants.inc](../kernel/src/includes/constants.inc).

## Model

| Property        | Value                                              |
|-----------------|----------------------------------------------------|
| Resolution      | 320 × 200                                           |
| Color depth     | 8 bpp (256-color palette)                           |
| Framebuffer     | physical `0xA0000`, mapped at `0xC00A0000`          |
| Pixel address   | `offset = y * 320 + x` (1 byte per pixel)           |
| Palette         | fixed 3-3-2 RGB (`RRRGGGBB`), loaded on `gfx_enter` |
| Buffering       | double-buffered (off-screen backbuffer + `gfx_blit`)|
| Input           | PS/2 mouse on IRQ12                                 |

The mode switch is done by writing the VGA register set directly
(`vga_write_regs`) — no BIOS call and no bootloader change. Entering graphics
mode does not disturb the shell's text contents; they reappear after
`gfx_exit` because the kernel restores the text font (see
[Font handling](#font-handling)).

## Lifecycle

A graphics program follows this shape:

```nasm
mov eax, 36        ; gfx_enter — switch to Mode 13h
int 0x80

.frame:
    ; ... draw into the backbuffer with gfx_clear / fillrect / line / ...
    mov eax, 45    ; gfx_blit — present the finished frame
    int 0x80
    ; ... read input, decide whether to continue ...
    jmp .frame

mov eax, 37        ; gfx_exit — restore the text shell
int 0x80
ret
```

**Always pair `gfx_enter` (36) with `gfx_exit` (37).** If a program returns to
the shell while still in Mode 13h, the shell keeps running but its output is
invisible (it is writing text into a graphics framebuffer). `gfx_exit` restores
text mode 03h, reloads the font, and clears the screen.

## Drawing primitives

All primitives draw into an **off-screen backbuffer**. Nothing appears on
screen until `gfx_blit` (45) copies the backbuffer to the visible framebuffer.
This gives flicker-free animation: build a whole frame, then blit once.

All coordinates are clipped to the 320×200 screen, so off-screen and partially
off-screen shapes are safe. `color` is always a 0–255 palette index.

| Syscall | Name           | Arguments                                          | Effect                              |
|---------|----------------|----------------------------------------------------|-------------------------------------|
| 38      | `gfx_clear`    | `ebx` = color                                      | Fill the whole backbuffer           |
| 39      | `gfx_pixel`    | `ebx` = x, `ecx` = y, `edx` = color                | Plot one pixel                      |
| 40      | `gfx_fillrect` | `ebx` = x, `ecx` = y, `edx` = color, `esi` = w, `edi` = h | Filled rectangle             |
| 41      | `gfx_rect`     | `ebx` = x, `ecx` = y, `edx` = color, `esi` = w, `edi` = h | 1-pixel outline rectangle    |
| 42      | `gfx_line`     | `ebx` = x0, `ecx` = y0, `edx` = x1, `esi` = y1, `edi` = color | Bresenham line (any angle) |
| 43      | `gfx_char`     | `ebx` = char, `ecx` = x, `edx` = y, `esi` = color  | One 8×16 glyph                      |
| 44      | `gfx_string`   | `esi` = ptr, `ebx` = x, `ecx` = y, `edx` = color   | NUL-terminated string, 8px advance  |
| 45      | `gfx_blit`     | —                                                  | Present the backbuffer              |
| 46      | `gfx_info`     | `edi` = 12-byte dst                                | Write width, height, bpp            |

`gfx_info` writes three dwords at `[edi]`: width (320), height (200), bpp (8).
Use it instead of hard-coding the resolution.

Text uses the same 8×16 BIOS font the shell uses (snapshotted at boot). `x` is
the left edge, `y` the top edge of the glyph cell; `gfx_string` advances 8
pixels per character with no wrapping.

## Colors — the 3-3-2 palette

`gfx_enter` programs all 256 DAC entries with a fixed palette in which **the
color index encodes its own RGB value**:

```
bit:  7 6 5  4 3 2  1 0
      R R R  G G G  B B
```

So you can construct a color arithmetically: `color = (R<<5) | (G<<2) | B`
where `R`,`G` ∈ 0–7 and `B` ∈ 0–3. This makes color choice deterministic — no
palette table to manage. Some useful values:

| Index  | Hex    | Color   |
|--------|--------|---------|
| 0      | `0x00` | black   |
| 3      | `0x03` | blue    |
| 28     | `0x1C` | green   |
| 31     | `0x1F` | cyan    |
| 224    | `0xE0` | red     |
| 227    | `0xE3` | magenta |
| 252    | `0xFC` | yellow  |
| 255    | `0xFF` | white   |

Internally each 3-bit field scales to the 6-bit DAC by ×9 (0→0, 7→63) and the
2-bit blue field by ×21 (0→0, 3→63), giving an even ramp across each channel.

## Mouse

`mouse_init` (called at boot) enables the PS/2 auxiliary device and IRQ12. The
IRQ12 handler decodes the 3-byte packet and maintains `mouse_x`, `mouse_y`
(clamped to 320×200) and a button mask. The cursor starts centered.

| Syscall | Name    | Arguments           | Effect                          |
|---------|---------|---------------------|---------------------------------|
| 47      | `mouse` | `edi` = 12-byte dst | Write x, y, buttons             |

`mouse` writes three dwords at `[edi]`: `[0]` = x, `[4]` = y, `[8]` = button
mask (bit 0 = left, bit 1 = right, bit 2 = middle). There is no hardware
cursor — draw your own (e.g. a small `gfx_fillrect` at the reported position)
each frame.

## A complete example

[commands/gdemo.asm](../commands/gdemo.asm) is a runnable demo: it enters
graphics mode, draws a filled rectangle, an outlined rectangle, a diagonal
line, a string, and a mouse cursor each frame, blits, and exits on a key press.
Sketch:

```nasm
mov eax, 36                 ; gfx_enter
int 0x80
.loop:
    mov eax, 38             ; gfx_clear (blue background)
    mov ebx, 0x03
    int 0x80

    mov eax, 40             ; gfx_fillrect (yellow)
    mov ebx, 20             ; x
    mov ecx, 20             ; y
    mov edx, 0xFC           ; color
    mov esi, 80             ; w
    mov edi, 50             ; h
    int 0x80

    mov eax, 47             ; read mouse into a local buffer
    lea edi, [ebp + mbuf]
    int 0x80
    mov eax, 40             ; draw the cursor as a small block
    mov ebx, [ebp + mbuf]       ; x
    mov ecx, [ebp + mbuf + 4]   ; y
    mov edx, 0x1C               ; green
    mov esi, 4
    mov edi, 4
    int 0x80

    mov eax, 45             ; gfx_blit — present the frame
    int 0x80

    mov eax, 7             ; get_key — exit on any key
    int 0x80
    test al, al
    jz .loop

mov eax, 37                 ; gfx_exit
int 0x80
ret
```

Run it from the shell:

```
gdemo
```

(Programs are position-independent — see the [ABI Contract](abi-contract.md)
for the `lea esi, [ebp + label]` addressing convention used above.)

## Performance notes

- A full-screen `gfx_blit` copies 64,000 bytes (`rep movsd`). At the 100 Hz
  PIT tick this is comfortably within one frame; throttle drawing to the tick
  (syscall 8 `get_tick`) rather than spinning.
- `gfx_fillrect` uses `rep stosb` per row; `gfx_clear` uses `rep stosd` over
  the whole buffer. Prefer one `gfx_clear` over many overlapping fills.
- Pixel-at-a-time drawing via `gfx_pixel` crosses the syscall boundary each
  call — use the rect/line/string primitives where possible.

## Internals (for kernel hackers)

| Concern        | Where / how                                                                 |
|----------------|------------------------------------------------------------------------------|
| Mode switch    | `vga_write_regs` streams a 61-byte register dump (`mode13_regs` / `text_regs` in [data.inc](../kernel/src/includes/data.inc)). |
| Framebuffer    | `0xA0000` is already identity-mapped at `0xC00A0000`; no new page table.     |
| Backbuffer     | `gfx_backbuf` (64,000 bytes) in [bss.inc](../kernel/src/includes/bss.inc), placed before `page_bitmap` so it sits in the reserved kernel range. |
| Palette        | `gfx_load_palette` writes 256×3 bytes to DAC port `0x3C9`.                   |
| Font handling  | See below.                                                                  |
| Mouse IRQ      | `set_irq12` installs the gate at IDT vector `0x2C`; `pic_remap` unmasks IRQ2 (cascade) and IRQ12. |

### Font handling

Mode 13h's chain-4 writes clobber the BIOS text font, which lives in VGA
plane 2. To keep the shell text intact across a graphics session:

1. At boot, `gfx_save_font` snapshots the 256×16 font into `font_save` (while
   still in text mode).
2. `gfx_char` / `gfx_string` render glyphs from that snapshot in graphics mode.
3. `gfx_exit` calls `gfx_load_font` to write the snapshot back into plane 2,
   restoring the shell font.

### Concurrency

There is no real concurrency on the framebuffer: only the foreground task
(the program launched by the shell) draws. The graphics helpers use fixed BSS
scratch variables (e.g. `fr_*`, `gfx_l*`), which is safe because no other task
calls them concurrently. The only asynchronous writer is the IRQ12 handler,
and it only touches the mouse state, never the framebuffer.

## Adding a graphics program

Same as any userland program (see [Userland](userland.md)):

1. Write `commands/yourprog.asm` (position-independent, `[org 0x00000000]`).
2. Add `yourprog` to the `COMMANDS=(…)` array in [build/asm](../build/asm).
3. `./asm -r`, then run `yourprog` from the shell.
