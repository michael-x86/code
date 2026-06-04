---
paths:
 - "kernel/src/includes/graphics.inc"
 - "kernel/src/includes/constants.inc"
 - "commands/gdemo.asm"
 - "commands/elite.asm"
 - "commands/invaders.asm"
 - "commands/enigma.asm"
 - "docs/graphics.md"
---

# VGA Mode 13h Graphics — Rules

**Last updated**: 2026-06-04

Applies to: `kernel/src/includes/graphics.inc`, the graphics syscall table in `kernel/src/includes/constants.inc`, and any userland program that switches into graphics mode (`commands/gdemo.asm`, `commands/elite.asm`, `commands/invaders.asm`, `commands/enigma.asm`, etc.). Read [docs/graphics.md](../docs/graphics.md) first — it is the canonical reference.

## The Model

| Property        | Value                                              |
|-----------------|----------------------------------------------------|
| Resolution      | 320 × 200                                           |
| Color depth     | 8 bpp (256-color palette)                           |
| Framebuffer     | physical `0xA0000`, mapped at virtual `0xC00A0000`   |
| Pixel address   | `offset = y * 320 + x` (1 byte per pixel)           |
| Palette         | fixed 3-3-2 RGB (`RRRGGGBB`), loaded on `gfx_enter` |
| Buffering       | double-buffered (off-screen backbuffer + `gfx_blit`) |
| Input           | PS/2 mouse on IRQ12                                 |

The mode switch is done by writing the VGA register set directly (`vga_write_regs`) — no BIOS call and no bootloader change. Entering graphics mode is a one-way door until `gfx_exit`; the text-mode shell is preserved by switching back when a graphical program exits.

## Subsystem Layout

- Engine: `kernel/src/includes/graphics.inc`
- Constants and syscall numbers: `kernel/src/includes/constants.inc` (search for `GFX_*`, `MOUSE_*`)
- Syscall dispatch: `kernel/src/includes/syscall.inc` (cases for gfx/mouse syscalls)
- Reference userland example: `commands/gdemo.asm`
- Active graphical programs: `elite.asm`, `invaders.asm`, `enigma.asm`, `gdemo.asm`

## Adding a Graphics Primitive

1. **Decide whether it belongs in the kernel engine** or in userspace.
   - Pixel-level or scanline-level primitives (rect, line, sprite, blit, scroll) → kernel syscall
   - Game logic, scene management, AI, animation → userspace
2. **If a kernel syscall**: pick a new syscall number (next free after the current gfx/mouse range), add the handler in `graphics.inc`, register the case in `syscall.inc`, update [docs/graphics.md](../docs/graphics.md), and update [docs/syscalls.md](../docs/syscalls.md) with the new number.
3. **If a userspace helper**: write it in the program itself, using existing syscalls. Don't add a syscall for "draw a circle" if the program can call `gfx_hline`/`gfx_vline` in a loop.
4. **Validate**: `./asm -r`, run the program, confirm the primitive renders correctly and `gfx_exit` returns cleanly to the text shell.

## Adding a New Palette

The default palette is 3-3-2 RGB. To override:

1. Define the new palette in `graphics.inc` (256 entries of 768 bytes total — 3 bytes per color)
2. Add a syscall or `gfx_set_palette` helper if the palette is dynamic; for static palettes, load it inside `gfx_enter` after the mode switch
3. Document the palette in [docs/graphics.md](../docs/graphics.md)
4. Test by drawing a known image and confirming colors

## Mouse Handling (IRQ12)

- The PS/2 mouse is on IRQ12. The handler reads three bytes (buttons, dx, dy) and pushes them onto a ring buffer.
- Userland reads via a `MOUSE_READ` syscall: returns the latest packet, or `-1` if the buffer is empty.
- Coordinate system: `(0, 0)` is top-left of the framebuffer (320×200), Y increases downward.
- Don't poll the mouse port from userspace — go through the syscall. The IRQ handler manages the byte-level protocol.

## Double Buffering

- The "real" framebuffer is at physical `0xA0000` (mapped `0xC00A0000`)
- The "backbuffer" is a separate 64 KB region allocated by the kernel (typically just after the heap)
- Draw into the backbuffer using the existing primitives, then call `gfx_blit` to copy the backbuffer to the framebuffer in one pass
- `gfx_blit` is the only way to "present" a frame. Direct writes to the framebuffer work but cause tearing.

## When Things Go Wrong

| Symptom                        | Likely cause                                | First check                                |
|--------------------------------|---------------------------------------------|--------------------------------------------|
| Mode switch fails / no display  | VGA register sequence wrong                 | `vga_write_regs` table in `graphics.inc`   |
| Garbage colors                 | Palette not loaded after mode switch        | Confirm `gfx_enter` writes the DAC palette |
| Tearing / flicker              | Drawing to framebuffer, not backbuffer      | Add `gfx_blit` at the end of the frame     |
| Mouse doesn't move             | IRQ12 not enabled / handler not installed   | `info pic` in QEMU monitor, check IRQ12    |
| Mouse coordinates wrong        | Sign extension on dy / packet parsing bug   | Check the IRQ12 handler byte 3              |
| `gfx_exit` returns to garbage  | VGA register sequence wrong on exit          | `vga_write_text_regs` table                |
| Frame rate low                 | Backbuffer not aligned / blit per-pixel      | Use `gfx_blit_rect` for partial updates    |

## Anti-Patterns (Don't)

- Don't write to physical `0xA0000` from userspace — go through the syscall (the address is mapped, but the page attributes may not allow it, and direct writes defeat the double-buffering model)
- Don't add a "draw circle" syscall — use `gfx_hline`/`gfx_vline` in a loop
- Don't add a new palette format beyond 3-3-2 RGB without documenting the change in [docs/graphics.md](../docs/graphics.md) and the syscall table
- Don't read the mouse port directly from userspace — use the `MOUSE_READ` syscall
- Don't "optimize" the IRQ12 handler by inlining the byte reads — the byte-level protocol has timing constraints, and the current handler is the result of several past regressions
- Don't draw a frame to the framebuffer (not the backbuffer) "to save the blit" — tearing is the cost

## Validation Gate

Before a graphics change ships:

1. `./asm` — clean assemble
2. `./asm -r` — boot
3. Run `commands/gdemo.asm` — confirm all primitives render and the program exits cleanly back to the text shell
4. Run any other graphical program you touched (`elite`, `enigma`, `invaders`, etc.)
5. Confirm `gfx_exit` returns the text shell with no garbage and the keyboard still works
6. If you changed the IRQ12 handler: step through with GDB (`break *<isr12_addr>`) and confirm the byte sequence is correct
7. If you changed the palette: dump a known image and confirm colors match the spec
8. If you added a new syscall: also run the "Regression: `int 0x80` dispatch" check from `.clinerules/syscalls.md` (run `cat`, `ping`, etc.)

## Reference

- [docs/graphics.md](../docs/graphics.md) — full graphics spec
- [docs/syscalls.md](../docs/syscalls.md) — gfx/mouse syscall numbers
- `kernel/src/includes/graphics.inc` — engine
- `kernel/src/includes/constants.inc` — `GFX_*` and `MOUSE_*` constants
- `commands/gdemo.asm` — reference userland example
- `commands/elite.asm` — large graphics consumer (BBC Micro Elite port)
- `commands/enigma.asm` — Enigma simulator (graphics + text)

---

**Last updated**: 2026-06-04
