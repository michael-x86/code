# Elite for x86 Assembly Kernel — Implementation Plan

A faithful homage to the BBC Micro Elite (1984), informed by the flicker-free
Elite-A source code by Angus Duggan and Mark Moxon's documentation.

## Vision

Recreate the core Elite space trading experience as a userland program for the
x86 kernel. Wireframe 3D ships, split-screen scanner, stardust, flicker-free
double-buffered rendering, and the procedural universe — all written in 32-bit
assembly, calling the kernel via `int 0x80`.

The game will ship as `/bin/elite` in the filesystem, loaded and executed
exactly like `invaders` or `vi`, but taking over the full screen in graphics
mode.

**Equally important:** the graphics subsystem built to enable Elite becomes a
general-purpose rendering layer available to all userland programs — terminal
emulators, spreadsheets, charting tools, anything that needs pixel-level
output.

## Layered Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Userland applications                                       │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────────────┐  │
│  │ /bin/    │  │ /bin/        │  │ /bin/                 │  │
│  │ elite    │  │ terminal     │  │ spreadsheet           │  │
│  │ (3D + UI)│  │ (text UI)    │  │ (grid + charts)       │  │
│  └────┬─────┘  └──────┬───────┘  └───────────┬───────────┘  │
│       │               │                      │              │
│  ┌────▼───────────────▼──────────────────────▼───────────┐  │
│  │              libgfx — Graphics Primitives Layer         │  │
│  │  pixel  draw_line  draw_hline  draw_vline             │  │
│  │  draw_rect  fill_rect  draw_circle  draw_ellipse      │  │
│  │  draw_char  draw_string  blit_rect  scroll_rect        │  │
│  │  set_clip_rect  dirty_rect  set_color  get_font_metric │  │
│  │  backbuffer_alloc  backbuffer_free                     │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                                │
│  ┌──────────────────────────▼────────────────────────────┐  │
│  │              Framebuffer Backbuffer Manager            │  │
│  │  Allocate backbuffer in heap from sys_fb_info         │  │
│  │  All drawing goes to backbuffer                       │  │
│  │  sys_fb_swap: memcpy backbuffer → visible framebuffer  │  │
│  │  Optional: dirty-rect partial flip for speed           │  │
│  └──────────────────────────┬────────────────────────────┘  │
├─────────────────────────────┼────────────────────────────────┤
│  Kernel                     │                                │
│  ┌──────────────────────────▼────────────────────────────┐  │
│  │  Framebuffer + syscall interface                       │  │
│  │  sys_fb_info (31): return fb addr, w, h, pitch, bpp  │  │
│  │  sys_fb_swap (32): flip backbuffer to visible fb      │  │
│  └───────────────────────────────────────────────────────┘  │
│  Existing: sys_get_key (7), sys_get_tick (8)                │
└──────────────────────────────────────────────────────────────┘
```

## libgfx — The General-Purpose Graphics Primitives Layer

This is the shared library that every userland program links against. It is
the reason the graphics work for Elite pays off for everything else.

### Design Principles
- **Position-independent:** linked into any flat binary at any base address
- **No syscalls for drawing:** all primitives write directly to the backbuffer
  memory that the program already has access to via sys_fb_info
- **One syscall at frame end:** sys_fb_swap to flip the backbuffer
- **Self-contained:** carries its own 8x16 bitmap font, no kernel dependency
- **Caller manages state:** backbuffer pointer, clip rect, draw color are
  passed explicitly or stored in a context struct

### Primitive Reference

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gfx_pixel` | x, y, color | Set one pixel |
| `gfx_draw_line` | x1, y1, x2, y2, color | Bresenham, clipped to clip rect |
| `gfx_draw_hline` | x, y, len, color | Optimized horizontal |
| `gfx_draw_vline` | x, y, len, color | Optimized vertical |
| `gfx_draw_rect` | x, y, w, h, color | Outline rectangle |
| `gfx_fill_rect` | x, y, w, h, color | Filled rectangle |
| `gfx_draw_circle` | cx, cy, r, color | Midpoint circle |
| `gfx_draw_ellipse` | cx, cy, rx, ry, color | Ellipse (radar scanner) |
| `gfx_draw_char` | x, y, ch, color | 8x16 bitmap glyph |
| `gfx_draw_string` | x, y, str, color | Null-terminated string |
| `gfx_blit_rect` | x, y, w, h, src, dst | Rect copy (scroll, compose) |
| `gfx_scroll_rect` | x, y, w, h, delta_y | Scroll content up/down |
| `gfx_set_clip` | x, y, w, h | Set clipping rectangle |
| `gfx_clear_clip` | — | Reset clip to full screen |
| `gfx_dirty_mark` | x, y, w, h | Mark region for partial flip |

### Backbuffer Management (in libgfx)

```nasm
; Called once at program start
gfx_init:
    mov eax, 31          ; sys_fb_info
    int 0x80
    ; returns: eax=fb_phys_addr, ebx=width, ecx=height,
    ;          edx=pitch, esi=bpp
    ; Allocate backbuffer: width * height * (bpp/8) bytes
    ; Store backbuffer pointer, stride, bpp in globals
    ret

; Called once per frame after all drawing
gfx_flip:
    ; Full flip: memcpy(backbuffer, framebuffer, size)
    ; Dirty flip: for each dirty rect, memcpy that strip
    mov eax, 32          ; sys_fb_swap (optional: pass dirty rect)
    int 0x80
    ret
```

### Font

Embedded 8x16 bitmap font, 256 glyphs = 4 KB. Covers ASCII 0x20-0x7E
(printable) plus box-drawing characters. Programs use `gfx_draw_char` /
`gfx_draw_string` instead of the kernel's `putchar` / `print`.

For terminal/spreadsheet use: the bitmap font gives immediate text rendering
without any kernel cooperation.

### How Applications Use libgfx

**Terminal emulator:**
```
fill_rect     → clear terminal area
draw_string   → render each character of the prompt line
draw_hline    → status bar separator
blit_rect     → scroll viewport (move pixel region up)
fill_rect     → draw cursor block
dirty_mark    → mark changed lines
gfx_flip      → show updated frame
```

**Spreadsheet:**
```
draw_hline + draw_vline  → cell grid lines
draw_string               → cell text content
fill_rect                 → selected cell highlight
fill_rect                 → column/row header backgrounds
draw_rect                 → active cell border
blit_rect                 → scroll grid region when navigating
draw_string               → formula bar
```

**Charting tool:**
```
draw_hline + draw_vline  → axes and gridlines
draw_string               → axis labels
draw_line                 → line chart segments
fill_rect                 → bar chart bars
draw_circle               → scatter plot dots
```

**Elite (the most complex consumer):**
```
fill_rect                 → clear sky, draw dashboard background
draw_line (via LL9)       → ship wireframes, planet, sun, stardust
draw_ellipse              → radar scanner
fill_rect                 → HUD bar indicators
draw_string               → HUD text (fuel, cash, target name)
draw_rect                 → viewport borders
blit_rect                 → flicker-free line interleave helper
```

### libgfx Source Structure

```
libgfx/
  gfx.asm              Init, backbuffer alloc/flip, clip state
  gfx_pixel.asm        Pixel plotting (bpp-aware)
  gfx_line.asm         Bresenham line + clip, hline fast-path, vline fast-path
  gfx_rect.asm         draw_rect, fill_rect
  gfx_circle.asm       Circle, ellipse midpoint algorithms
  gfx_text.asm         draw_char, draw_string, font data (4 KB)
  gfx_blit.asm         blit_rect, scroll_rect
  gfx_dirty.asm        Dirty rectangle tracking for partial flip
  gfx.inc              Macro definitions, function pointer table
```

### Integrating libgfx into a Userland Program

```nasm
[bits 32]
[org 0x00000000]

%include "libgfx/gfx.inc"    ; imports + context struct

section .text
_start:
    call gfx_init            ; alloc backbuffer, get fb info

.loop:
    call gfx_fill_rect       ; clear
    call do_game_logic       ; your program here
    call do_rendering        ; draw everything
    call gfx_flip            ; show frame

    call wait_for_tick       ; vsync / 60fps cap
    jmp .loop
```

## Kernel Changes (Graphics + Sound Subsystem Depends on These)

From `graphics-plan.md` Phase 1-3, plus sound support:

### Bootloader — Mode 13h Switch
- In `bootloader.asm`, before entering protected mode:
  - `mov ax, 0x0013` / `int 0x10` — set VGA Mode 13h (320x200x8)
  - That's it. No VBE query, no mode info block, no 256-byte buffer needed
  - Framebuffer at 0xA0000 is already identity-mapped by existing PDE 0
  - No page table changes needed at all
  - Store a single byte flag at 0x500: `[graphics_mode]` = 0x13

### Kernel — Syscall
- In `syscall.inc`: add syscall handlers (no page table changes needed)
  - **31: sys_fb_info** — returns: eax=0xA0000 (phys fb addr),
    ebx=320, ecx=200, edx=320 (pitch), esi=8 (bpp)
  - **32: sys_fb_swap** — memcpy backbuffer to 0xA0000 (or userland does it)

### I/O Permission Bitmap (for Sound)

Each TSS has an 8192-bit I/O permission bitmap. A cleared bit = port accessible
from ring 3. Default is all 1s (nothing accessible). The kernel already sets
up TSS for each task; `sys_ioperm` sets bits in the current task's bitmap.

Elite and other sound-capable programs need ports 0x42, 0x43, 0x61. The
program calls `sys_ioperm` once at startup, then uses `out` directly. No
kernel involvement per-sound-event.

Alternative: reserve syscall 33 for sound commands directly (set frequency,
play preset). But `sys_ioperm` is more flexible — it lets the program drive
the PIT and speaker however it wants, enabling richer sound engines.

### Whether sys_fb_swap is a Syscall or Userland Memcpy

Two valid approaches:

**Option A — Userland memcpy (simpler, recommended first):**
- sys_fb_info returns the physical framebuffer address
- program writes directly to a heap-allocated backbuffer
- `gfx_flip` is `memcpy(framebuffer, backbuffer, size)`
- No kernel involvement in the flip
- Works because the framebuffer is mapped into the program's page tables
  (kernel sets this up, or the program maps it via sys_fb_info)

**Option B — Kernel-assisted swap (faster, later):**
- sys_fb_swap takes backbuffer address + size
- kernel does the memcpy with interrupts off (faster, no page faults)
- Enables kernel-managed dirty-rect partial updates

Start with Option A. Upgrade to Option B after Elite is running and
profiling shows the full-screen memcpy is a bottleneck.

## Source Structure for Elite

```
elite/
  elite.asm              Entry point, main loop, init
  game/
    flight_loop.asm      Main flight loop (equivalent to TT170)
    docking.asm          Docking computer, station approach
    tactics.asm          Enemy AI, combat, missile logic
    market.asm           Buy/sell cargo, price generation
  engine/
    ship_draw.asm        LL9 — main ship drawing routine (12 parts)
    ship_draw_flicker.asm  LSPUT — flicker-free line interleaving
    ship_draw_dot.asm    SHPPT — distant ship as dot
    planet_draw.asm      PLANET — crater, meridian, equator
    sun_draw.asm         SUN — shimmering sun
    stardust.asm         Stardust particles
    line_math.asm        Fixed-point perspective, projection helpers
    collision.asm        Edge/face visibility, back-face culling
  math/
    vectors.asm          Orientation vectors (nosev, roofv, sidev)
    matrix.asm           Rotation matrix construction
    trigonometry.asm     Sine/cosine lookup tables (256 entries each)
    random.asm           Galaxy seed twisting (TAWS algorithm)
  data/
    ships.asm            Ship blueprints (vertices, edges, faces)
    galaxies.asm         Galaxy seed data
    text_tokens.asm      Compressed text token tables
  inc/
    constants.asm        Screen dims, ship counts, type equates
    macros.asm           Shared macros
```

Elite links against `libgfx` for all drawing operations. The game's 3D engine
(Math + engine/ folder) computes what to draw; libgfx does the actual pixel
work.

## Graphics Subsystem Decisions

### Mode: Mode 13h (320x200x8-bit palette)
- Set via INT 10h AX=00h (mode 0x13) in bootloader — single byte change,
  no VBE info block, no mode query, no new page table needed
- Framebuffer at physical 0xA0000, already identity-mapped in PDE 0
- 256 colors via VGA DAC palette (ports 0x3C8/0x3C9)
- Higher resolution modes (VBE) can be added later — all drawing code is
  dimension-agnostic and will work at any resolution without changes

### Palette
Mode 13h gives us 256 programmable colors. We define a 64-color palette
that mimics the BBC Micro's feel with room for UI elements:

```
  0:  0x000000  Black (space background)
  1:  0x0000AA  Blue (hyperspace, scanner)
  2:  0x00AA00  Green (dashboard text, radar, HUD bars high)
  3:  0x00AAAA  Cyan (stardust, targeting)
  4:  0xAA0000  Red (laser, warnings, HUD bars low, sun corona)
  5:  0xAA00AA  Magenta (explosion flash)
  6:  0xAAAA00  Yellow/brown (sun surface, planet terrain)
  7:  0xAAAAAA  White/grey (ship wireframes, stardust, text)
  8-63: shades for gradients, UI chrome, planet variety
 64-255: reserved / unused
```

The palette is set once at game init. All drawing functions use color indices
(0-255), so if a pixel format is later set via VBE the color mapping
changes but drawing code does not.

### Backbuffer (Double Buffer)
- All drawing writes to backbuffer in heap memory
- Single memcpy flip at end of each frame — tear-free
- Mode 13h backbuffer: 320x200x1 byte = 64 KB — tiny
- Backbuffer allocated via `alloc_pages` from kernel heap

### Flicker-Free Rendering (the LSPUT algorithm)
Adapted from the BBC Master / Apple II version, not the original BBC Micro:

```
For each line of new ship (in order):
  1. Calculate screen coords for new line endpoints
  2. Draw new line to backbuffer (via libgfx draw_line)
  3. Fetch old line from ship line heap at position LSNUM
  4. Erase old line from backbuffer (draw in bg color)
  5. Store new line in heap at LSNUM (replace old)
  6. Advance LSNUM

After all new lines drawn:
  7. Erase any remaining old lines in heap (ship shrank)
  8. Draw laser if firing
```

Every new line is drawn before its corresponding old line is erased. The ship
is never blank on screen. Flicker drops to near-zero even without the final
backbuffer flip.

## 3D Engine Design

### Coordinate System (BBC Micro convention)
- Screen: origin at top-left, x right, y down
- Space: x right, y up, z into screen (right-handed)
- Fixed-point: 16.16 signed integers (32-bit), or 8.8 where precision allows

### Ship Data Block (37 bytes, NI% = 0x25)
```
  0-1:  x position (32-bit fixed-point 16.16)
  2-3:  y position (32-bit fixed-point 16.16)
  4-5:  z position (32-bit fixed-point 16.16)
  6-8:  orientation vectors (nosev, roofv, sidev) — 18 bytes
 24:    speed
 25:    acceleration
 26:    rotation state
 27:    AI state / hostility
 28:    energy
 29:    missiles
 30:    ship type (index into blueprint array)
 31:    line heap pointer (offset into shared heap)
 32:    laser / firing state
 33:    alive / exploding flag
 34-36: explosion counter, etc.
```

### Ship Blueprint Format
Each ship type defines:
- Number of vertices, edges, faces
- Vertex coordinates (relative to ship center)
- Edge list (pairs of vertex indices)
- Face list (surface normal vector + visibility distance)
- Max edges (for line heap size)
- Laser vertex index
- Bounty / cargo type predefined per type

Total: ~15-30 bytes per ship for a simplified model.

The full BBC Micro Elite has 31 ship types. For the homage, target **12 types**:
Sidewinder, Viper, Cobra Mk III, Krait, Adder, Gecko, Worm, Transporter,
Space Station, Asteroid, Missile, Cargo Canister.

### The LL9 Ship Drawing Routine (12-Stage Pipeline)
Directly adapted from the BBC Micro source:

```
LL9 Part 1:  Is it a planet/sun? → PLANET/SUN
             Is it behind us? → erase + return
             Is it exploding? → init explosion cloud
LL9 Part 2:  Outside screen bounds? → erase + return
             Flag laser vertex
             Calculate distance for visibility checks
             Too far? → SHPPT (draw as dot)
LL9 Part 3:  Fetch + normalize orientation vectors
             Get ship x,y,z in world space
LL9 Part 4:  If exploding → all faces visible, skip to part 6
LL9 Part 5:  Back-face culling via dot product
             Determine visible faces
LL9 Part 6:  Vertex visibility from associated faces
             Calculate visible vertex world coordinates
LL9 Part 7:  Apply perspective projection to vertex coords
LL9 Part 8:  Convert to screen coordinates
LL9 Part 9:  Erase old ship from backbuffer (line heap)
             If firing, calculate laser line coords
LL9 Part 10: Determine visible edges (from visible faces)
LL9 Part 11: Store visible edges in ship line heap
LL9 Part 12: Draw new ship lines (with LSPUT interleave)
```

### Perspective Projection
The BBC Micro uses a simple divide:
```
screen_x = (x * 256) / z + center_x
screen_y = (y * 256) / z + center_y
```
`256` is the scale factor (focal length). On x86 with 32-bit math we use
16.16 fixed-point or 64-bit intermediate results for precision.

### Back-Face Culling
The dot product trick from the original:
```
line_of_sight = ship_position + face_normal
dot = line_of_sight · face_normal

if dot < 0 → face visible (pointing toward viewer)
if dot > 0 → face hidden (pointing away)
```

Mathematically elegant, cheap to compute, and the reason Elite's ships
look solid rather than see-through.

### Flicker-Free Planet Drawing
The BBC Master's flicker-free algorithm works for planets too. For the homage,
include a secondary routine that:
1. Stores crater/meridian line segments in a "ball line heap"
2. Interleaves old-line-erase with new-line-draw (same LSPUT approach)
3. Requires extra heap memory (ball line heap is separate from ship heap)

Note: the BBC Micro cassette original could NOT do flicker-free planets due
to memory constraints. For our x86 system with 12 MB heap, this is trivial.

## Math Library

All fixed-point, no FPU required (matching 6502 authenticity):

| Operation | Implementation |
|-----------|---------------|
| Multiply | 16.16 × 16.16 → 16.16 (32-bit × 32-bit → 64-bit intermediate) |
| Divide | 16.16 / 16.16 → 16.16 (64-bit intermediate) |
| Sine/Cosine | 256-entry lookup table, 16-bit fixed-point values |
| Dot product | 3 multiplies + 2 adds |
| Cross product | 6 multiplies + 3 subtracts |
| Square root | Integer sqrt via binary search (for distance) |
| Random | Galaxy seed twist algorithm (LFSR-based, from original) |

The sine/cosine table is 256 entries × 2 bytes = 512 bytes.
A tangent table for 0-45 degrees adds 128 bytes.
These are embedded in the binary as read-only data. libgfx also embeds these
for its own use (circle/ellipse trig).

## Input Handling

Keyboard via sys_get_key (syscall 7):
- A/D or Left/Right arrows = roll
- W/S or Up/Down arrows = pitch
- Space = fire laser
- Tab = activate thrust (or just automatic)
- Escape = quit to shell

On the BBC Micro, flight controls use the keyboard row where Z/X control
roll and <>/, control pitch. We map this to modern WASD/arrows.

## Procedural Universe Generation

Elite's universe uses a seed-twist algorithm based on three 16-bit seeds
per system. Twisting produces:
- System name (from paired two-letter tokens)
- Coordinates (x, y in galaxy)
- Tech level, government, economy
- Population
- Productivity
- Random number seed for market prices

This is pure integer math — no randomness, fully deterministic. Galaxy 0
(seed = 0) is always the same. This is feasible in x86 assembly and a
delightful homage feature.

## Game Screens

From Elite-A, the views:

| View Key | View Name | Content |
|----------|-----------|---------|
| f0 | Front view | Main flight view (space) |
| f1 | Rear view | Behind ship |
| f2 | Left view | Port side |
| f3 | Right view | Starboard side |
| f4 | Gallery view | See ship from outside |
| f5 | Front with dash | Front + dashboard overlay |
| f6 | Data on system | System data text |
| f7 | Market prices | Buy/sell cargo |
| f8 | Status | Commander status |
| f9 | Inventory | Equipment list |

For the homage, implement at minimum:
- Front view (f0) — the main game
- Data on system (f6) — text screen, shows system name, government, etc.
- Market prices (f7) — buy/sell trading

## Split-Screen Display

The BBC Micro's iconic split screen shows the 3D space view at the top and
the dashboard at the bottom. In VGA text mode this was a hardware raster
split. In graphics mode, we simulate it:

```
┌────────────────────────────────────────┐
│                                        │
│         Space View (3D wireframes)     │
│         Ships, stardust, planet, sun   │
│                                        │
├────────────────────────────────────────┤
│  Scanner    Fuel  Energy  Missiles     │
│  (radar)    (bar)  (bar)  (count)     │
│  Legal      Cash   Target  Hyperspace  │
│  Status     (num)  (name)  (range)     │
└────────────────────────────────────────┘
```

Drawn as two separate framebuffer rectangles:
- Top: viewport rect (ship drawing via LL9 + libgfx lines)
- Bottom: dashboard area (bar indicators, text via libgfx draw_string,
  radar via libgfx draw_ellipse)

## Sound — Generated via PC Speaker (PIT Channel 2)

### Why Generated Sound

The BBC Micro Elite used the Texas Instruments SN76489 — a 4-channel sound
chip with 3 square-wave tone generators + 1 noise channel. Our x86 hardware
has nothing equivalent built-in. The only universally available option is the
PC speaker, driven by PIT channel 2 (8253/8254).

The PC speaker is extremely limited: a single square wave, turned on and off
via bit 1 of I/O port 0x61. There's a single PIT channel (channel 2) that
can generate a tone at a programmed frequency. That's it.

But that's actually *more* than the original BBC Micro had available for
game sound effects. The SN76489 could produce 3 simultaneous tones plus
noise; many NES/SMS games with richer sound had it easy. For a BBC Micro
homage, a single squawker is period-accurate and sufficient.

The trick to making the single PC speaker sound good is **procedural
sound generation** — rapidly modulating the PIT frequency and gate state
in software to simulate multiple voice channels.

### Hardware Interface

```
PIT Channel 2 → PC speaker
  I/O port 0x42: frequency divisor (write low byte, then high byte)
  I/O port 0x43: command register (write mode/command byte)
  I/O port 0x61: gate + enable (bits 0-1)

Bit pattern for port 0x61:
  bit 0: gate — PIT channel 2 output enables speaker (1=on, 0=off)
  bit 1: data — speaker data enable (1=PIT output drives speaker)

Program PIT channel 2 in mode 3 (square wave generator):
  Command byte = 0xB6  (channel 2, write lo/hi, mode 3, binary)
  Divisor = 1193182 / frequency
  E.g. divisor 1193 → ~1000 Hz tone
```

### Procedural Sound Model: Virtual Voices

We simulate multiple sound "voices" by rapidly toggling the speaker at
different effective rates. Instead of true polyphony (which the hardware
can't do), we use **time-division multiplexing** in a high-frequency timer
hook — IRQ0拦截 PIT channel 0's interrupt, running a software mixer at
100 Hz (tick rate) to recompute mixed waveforms:

```
Per tick (100 Hz):
  For each active voice:
    1. Advance voice phase accumulator by phase step
    2. Output: sign(phase accumulator) → square wave sample
  Mix all voices: sum of sample values → threshold at 0 → speaker bit
  Write frequency-divisor to PIT channel 2 for the mixed "carrier"
```

Actually, with a single-bit output (speaker is on or off), a simpler approach
works better:

```
Mixing at 100 kHz using PIT channel 2 direct frequency control:
  Each voice has a frequency (divisor) and envelope (amplitude 0-3)
  At 100 Hz game tick, recompute effective frequency from active voices
  One dominant voice at a time, with rapid switching simulating texture
```

**Practical approach for this project:** don't try to mix in software at
audio rates. Instead, use the 100 Hz tick to switch between voice
definitions — each voice has a pitch, duration, and envelope shape, and we
activate one at a time with smooth transitions. This is how the original
Elite worked: sound effects were prioritized, the highest-priority sound
won the single hardware channel.

### Sound Effect Definitions

The original BBC Micro Elite defined sound effects as 4-byte blocks in the
`SFX` table, with a simple envelope model driven by `NOS1`/`NO3`. We adapt
this to the PC speaker with a richer system:

**Sound voice structure (8 bytes):**
```
  0-1:  base_frequency    — PIT divisor for this voice's pitch
  2:    duration_frames   — how long this sound plays (in 100 Hz ticks)
  3:    envelope_type     — attack/decay/sustain/release shape
  4:    slide_rate        — frequency change per tick (signed, for sweeps)
  5:    pulse_width       — duty cycle texture (for timbral variety)
  6:    priority          — higher number = overrides lower sounds
  7:    flags             — bit 0: loop, bit 1: continuous
```

**Envelope types (ADSR):**
```
  0: instant-on, instant-off (click)
  1: short attack, instant decay (laser zap)
  2: medium attack, long decay (explosion)
  3: slow attack, full sustain (engine hum)
  4: fast attack, sustain with slow decay (ECM, fuel scooping)
  5: warble up (docking approach)
  6: warble down (docking complete)
  7: alarm pulse (proximity warning, energy low)
```

### Sound Effects for Elite

From the original BBC Micro Elite's SFX table and observed behavior:

| # | Name | Description | Frequency Behavior | Duration |
|---|------|-------------|-------------------|----------|
| 0 | `SND_LASER_US` | Our laser fire | Short high burst | 3 ticks |
| 1 | `SND_LASER_HIT` | We're being hit | Descending tone | 8 ticks |
| 2 | `SND_EXPLODE_US` | We died | White noise burst (rapid random toggle) then fade | 20 ticks |
| 3 | `SND_EXPLODE_THEM` | Enemy killed | Two-part: hit then boom | 24 ticks |
| 4 | `SND_BEEP` | Missile lock acquired | Short high pip | 2 ticks |
| 5 | `SND_BEEP_LOW` | Missile lock lost / energy low | Single low beep | 4 ticks |
| 6 | `SND_ENGINE` | Engine idle hum | Continuous mid tone, pitch follows speed | loop |
| 7 | `SND_ECM` | ECM activated | Modulated sweep up | 15 ticks |
| 8 | `SND_ECM_BURN` | ECM timed out | Descending chirp | 15 ticks |
| 9 | `SND_HYPERSPACE` | Hyperspace jump | Rising sweep (buildup) → cutoff → re-entry sweep | 60 ticks |
| 10 | `SND_HYPER_FAIL` | Hyperspace misjump | Rising sweep → harsh cutoff | 30 ticks |
| 11 | `SND_DOCK_APPROACH` | Docking computer engaged | Warble pattern (alternating tones) | loop |
| 12 | `SND_DOCKED` | Docking complete | Descending major triad approximation | 20 ticks |
| 13 | `SND_BOUNTY` | Kill confirmation (tally multiple of 256) | "Right on commander" beep sequence | 12 ticks |
| 14 | `SND_FUEL_SCOOP` | Scooping fuel/cargo | Low rising tone → click | 10 ticks |
| 15 | `SND_CRASH` | Collision with station/planet | Loud impact noise (rapid toggle) | 15 ticks |
| 16 | `SND_DOCK_DENIED` | Docking request denied | Low double-beep | 6 ticks |

That's 16 distinct sound effects, matching the richness of the original
BBC Micro version. All generated from a single software-mixed voice on the
PC speaker.

### Sound Engine API

```
; snd_init — call once at game start
snd_init:
    ; Program PIT channel 2 for initial silence
    ; Set up sound state (no active voice)
    ret

; snd_play — trigger a sound effect
;   in: al = sound number (0-15)
snd_play:
    ; Look up voice definition
    ; If priority >= current voice priority, activate
    ; Set current voice state from definition
    ret

; snd_update — call once per frame (from 100 Hz tick handler)
snd_update:
    ; If no active voice, turn speaker off and return
    ; Decrement duration counter
    ; If duration == 0, look for next queued voice or silence
    ; Apply envelope: modify frequency based on envelope type + tick counter
    ; Apply frequency slide
    ; Write new divisor to PIT channel 2
    ; Set/clear speaker gate bit in port 0x61
    ret

; snd_engine_update — call once per frame to modulate engine sound
;   in: al = speed (0-255, from player's velocity)
snd_engine_update:
    ; Map speed to divisor table
    ; Smooth transition to new frequency
    ret
```

### Speaker Gate Control

A helper for enabling/disabling the speaker cleanly:

```
speaker_on:
    in al, 0x61
    or al, 0x03        ; set bits 0 (gate) and 1 (data)
    out 0x61, al
    ret

speaker_off:
    in al, 0x61
    and al, 0xFC       ; clear bits 0-1
    out 0x61, al
    ret
```

### PIT Channel 2 Programming

```
; snd_set_frequency
;   in: ax = divisor (1193182 / desired_hz)
snd_set_frequency:
    push eax
    mov al, 0xB6        ; channel 2, lo/hi byte, mode 3, binary
    out 0x43, al
    pop eax
    out 0x42, al        ; low byte
    mov al, ah
    out 0x42, al        ; high byte
    ret
```

### Example: Laser Sound

The original Elite fires a short ascending then descending tone. With the PC
speaker this becomes:

```
laser_frames:
    tick 0: divisor 400  (~2983 Hz)  — attack, speaker on
    tick 1: divisor 300  (~3977 Hz)
    tick 2: divisor 200  (~5966 Hz)  — peak
    tick 3: divisor 300  (~3977 Hz)  — decay
    tick 4: divisor 400  (~2983 Hz)
    tick 5: silence, speaker off
```

Only 6 ticks at 100 Hz = 60ms of audio. That's the entire sound of a laser
blast. Short and punchy, instantly recognisable.

The same approach scales to explosions (20+ ticks of decreasing frequency
with noise-like modulation) and the hyperspace jump (60 ticks of sweeping
frequency).

### Noise Simulation

True noise requires randomness. The PC speaker can't generate it natively,
but we can toggle the speaker rapidly using a pseudo-random duty cycle:

```
; Generate explosion-like noise for N frames
; Uses the lower bits of the tick counter as a cheap PRNG
make_noise:
    mov ecx, eax        ; frame counter
.noise_loop:
    mov eax, [tick_count]
    and al, 0x01        ; toggle every other tick for noise texture
    shl al, 1
    or al, 0x01         ; set gate bit
    out 0x61, al
    ; Small spin loop for ~50us of noise
    push ecx
    mov ecx, 50
.spin: loop .spin
    pop ecx
    dec ecx
    jnz .noise_loop
    ret
```

This isn't true white noise, but the rapid random-ish toggling on the speaker
cone produces a satisfying crunchy explosion texture. The original Elite
used the SN76489's hardware noise channel (pseudo-random shift register) —
we're doing the same thing in software.

### Sound in the Kernel? Or Userland?

Sound hardware is accessible via I/O ports (0x42, 0x43, 0x61), which are
only available at ring 0 or when I/O permissions are granted. Options:

**Option A — Userland direct port access (simplest):**
- Requires the kernel's TSS I/O bitmap to allow ports 0x42, 0x43, 0x61
- Elite (and any sound-aware program) does `out` directly
- No kernel changes needed beyond I/O permission bitmap

**Option B — sys_snd syscall (more controlled):**
- Add syscall 33: `sys_snd` — al=subcommand (0=off, 1=set freq, 2=play preset)
- Sound mixing happens in kernel space on tick interrupt
- Cleaner abstraction, no userland port I/O needed
- The kernel already owns IRQ0 (PIT tick), so the mixer hooks naturally

**Option C — Userland mixer with kernel tick hook registration:**
- Add syscall 33: `sys_register_tick_hook` — userland provides a callback
- Kernel calls the callback on every tick (100 Hz), userland code does
  the sound mixing and port I/O is Option A's bitmap approach

**Recommendation: Option A first** (direct port I/O with relaxed I/O bitmap).
It's the fastest path to getting sound. The I/O bitmap is a per-task field
in the TSS; the kernel already sets up TSS structures for each task. Add a
field to the task struct that tracks which ports are allowed, and add a
syscall to request a port range.

### I/O Permission Bitmap

Each TSS has a 8192-bit I/O permission bitmap (one bit per port). A bit of
0 = port accessible; 1 = port denied. Default is all 1s (nothing accessible).

For Elite and graphical programs, we need to allow:
- `0x42, 0x43`: PIT channel 2 + command register (speaker sound)
- `0x61`: System control port (speaker gate + keyboard)

```
; Add to sys_snd (syscall 33) or a new syscall 33: sys_ioperm
;   in: ebx = port number, ecx = count, edx = enable (1=allow, 0=deny)
sys_ioperm:
    ; Find the current task's TSS
    ; For each port in [ebx, ebx+ecx):
    ;   Set the corresponding bit in the I/O bitmap
    ;   Allow = clear bit; deny = set bit
    ret
```

Note: the I/O bitmap only affects ring 3. If Elite runs in userland (ring 3),
this is the correct mechanism. If the kernel does sound mixing (Option B), no
bitmap changes needed.

### Sound Priority System

Just like the original Elite, sounds have priorities. When two sounds try to
play simultaneously, the higher-priority sound wins:

```
Priority 4: Death / explosion (unmissable)
Priority 3: Hyperspace, hyperspace misjump
Priority 2: Laser fire, collision, fuel scoop
Priority 1: Beeps, alarms, engine sound
Priority 0: Ambient (engine hum — always overridden)
```

Within the `snd_update` handler per tick, the highest-priority active voice
gets the speaker.

### Memory Budget for Sound

| Component | Size |
|-----------|------|
| Sound voice definitions (16 voices × 8 bytes) | 128 bytes |
| Sound engine state (current voice, timer, phase) | ~32 bytes |
| Sine/triangle table for warble sounds | 256 bytes |
| Total | ~416 bytes |

Negligible. The sound engine is tiny.

### libgfx Sound Module

If we unify sound into the graphics library:

```
libgfx/
  ... (existing gfx modules)
  snd.asm              Sound engine: init, play, update
  snd_voices.asm       Voice definitions (16 sound effects)
  snd.inc              Constants: SND_LASER_US, SND_ENGINE, etc.
```

This way any application (not just Elite) can play sound through a simple
API. The sound engine runs purely on port I/O, no kernel cooperation needed.

## Memory Budget

In the 12 MB kernel heap:

| Component | Size |
|-----------|------|
| Backbuffer (320x200x8-bit) | 64 KB |
| libgfx code + font | ~8 KB |
| Sound engine (voices + state + tables) | ~416 bytes |
| Ship line heap (256 lines x 4 bytes x 18 ships) | 18 KB |
| Ball line heap (planet/sun) | ~2 KB |
| Ship data blocks (18 ships x 37 bytes) | ~666 bytes |
| Ship blueprints (12 types) | ~3 KB |
| Math tables (sin/cos/tan) | ~1 KB |
| Stardust particles (18 x coords) | ~216 bytes |
| Galaxy seeds + text tokens | ~2 KB |
| Game state + variables | ~1 KB |
| Code (.text) all sub-areas | ~40 KB |
| **Total** | **~140 KB** |

Plenty of heap remaining. Other applications (terminal, spreadsheet) allocate
their own backbuffers and state independently.

## Implementation Order

### Milestone 0 — Kernel Graphics Foundation
**Depends on:** graphics-plan.md Phase 1-3 (simplified for Mode 13h)

- [ ] Mode 13h switch in bootloader: `mov ax, 0x0013` / `int 0x10`
- [ ] `sys_fb_info` syscall (number 31) — return fb addr=0xA0000, w=320, h=200, pitch=320, bpp=8
- [ ] `sys_fb_swap` syscall (number 32) — optional, userland memcpy works too
- [ ] `sys_ioperm` syscall (number 33) — for PC speaker I/O port access
- [ ] Update `constants.inc` with `VGA_FB_PHYS equ 0xA0000`
- [ ] Test: bootloader switches to graphics mode, kernel runs, screen is mode 13h

Note: no page table changes needed. Framebuffer at 0xA0000 is already
identity-mapped by the existing PDE 0. This is the win of Mode 13h — the
graphics-plan page table Phase 2 is completely skipped.

### Milestone 1 — libgfx General-Purpose Layer
**Depends on:** Milestone 0

- [ ] `gfx.asm`: init (sys_fb_info + alloc backbuffer), clear, flip
- [ ] `gfx_pixel.asm`: pixel plot (8-bit indexed, color index in register)
- [ ] `gfx_line.asm`: Bresenham line with clip, hline/vline fast-paths
- [ ] `gfx_rect.asm`: draw_rect (outline), fill_rect (solid)
- [ ] `gfx_circle.asm`: midpoint circle + ellipse
- [ ] `gfx_text.asm`: 8x16 font data (4 KB), draw_char, draw_string
- [ ] `gfx_blit.asm`: blit_rect (memcpy strip), scroll_rect
- [ ] `gfx_dirty.asm`: dirty rectangle tracking
- [ ] Test: `/bin/gfx_test` — fills screen with colored rects, draws text,
  shows a circle, flips, exits

### Milestone 2 — First libgfx Consumer: Enhanced Terminal
**Depends on:** Milestone 1

- [ ] Write `/bin/termu` — pixel-mode terminal emulator using libgfx
- [ ] Demonstrates: fill_rect for bg, draw_char/draw_string for text,
  blit_rect for scroll, dirty_rect for partial flip
- [ ] Proves the layer works for non-game software before investing in 3D

### Milestone 2.5 — Sound Engine
**Depends on:** Milestone 1 (for tick timing), syscall 33 (sys_ioperm)

- [ ] `libgfx/snd.asm`: sound engine (init, play, update, speaker on/off)
- [ ] `libgfx/snd_voices.asm`: 16 voice definitions (laser, explosion, etc.)
- [ ] `libgfx/snd.inc`: sound effect constants
- [ ] Add `sys_ioperm` (syscall 33) to kernel for I/O port access
- [ ] Test: `/bin/snd_test` — plays each sound effect in sequence
- [ ] Test: `/bin/termu` gets a terminal bell via snd_play(SND_BEEP) on char 7

### Milestone 3 — Elite 3D Ship Drawing
**Depends on:** Milestone 1

- [ ] Ship type constants and blueprint data (`data/ships.asm`)
- [ ] `math/` library: fixed-point multiply, divide, sin/cos/tan tables
- [ ] `vectors.asm`: orientation vector math
- [ ] Perspective projection: world coords → screen coords
- [ ] Visibility distance culling
- [ ] Draw a single static ship wireframe (no rotation, no erasing)
- [ ] Rotate with keys, erase+redraw each frame (flicker expected)

### Milestone 4 — Elite Flicker-Free Rendering
**Depends on:** Milestone 3

- [ ] Ship line heap allocation and pointer management
- [ ] Implement LSPUT: draw new line / erase old line interleave
- [ ] Back-face culling via the dot product method
- [ ] Full 12-stage LL9 pipeline
- [ ] LL9 Part 12 integrated with LSPUT
- [ ] Test: solid non-flickering ship that rotates cleanly

### Milestone 5 — Elite Game Shell
**Depends on:** Milestone 4

- [ ] Game entry point: init, defaults
- [ ] Keyboard input mapping (A/D=roll, W/S=pitch, Space=fire)
- [ ] Player ship physics: pitch, roll, velocity, position
- [ ] Single enemy ship AI: approach, fire, evade states
- [ ] Ship spawning in local bubble (K% workspace)
- [ ] Collision detection: laser hits ship
- [ ] Shield energy and player damage
- [ ] Score counter (kills × type multiplier)

### Milestone 6 — Elite Space Environment
**Depends on:** Milestone 5

- [ ] Planet drawing: crater, meridian, equator lines
- [ ] `planet_draw.asm`: PLANET routine
- [ ] `sun_draw.asm`: SUN — shimmering sun with red/yellow lines
- [ ] `stardust.asm`: moving particles with wraparound
- [ ] Space station (special ship type, circular drawing)
- [ ] Multiple ships in local bubble (up to 18)

### Milestone 7 — Elite Dashboard and Scanner
**Depends on:** Milestone 6

- [ ] Horizontal bar indicators (fuel, energy, shields, heat) via libgfx
- [ ] 3D elliptical radar scanner via libgfx draw_ellipse
- [ ] Legal status, cargo, hyperspace fuel, cash display via libgfx
- [ ] Missile indicator box
- [ ] Target name display
- [ ] Split-screen border

### Milestone 8 — Elite Trading and Progression
**Depends on:** Milestone 7

- [ ] Procedural galaxy generation (seed-twist algorithm)
- [ ] System data screen (f6)
- [ ] Market prices screen (f7)
- [ ] Buy/sell cargo
- [ ] Hyperspace jump between systems
- [ ] Rank progression (kill count)
- [ ] Equipment purchasing: lasers, fuel scoop, ECM

### Milestone 9 — Elite Docking and Missions
**Depends on:** Milestone 8

- [ ] Docking computer (auto-pilot to space station)
- [ ] Docking tunnel animation
- [ ] Launch sequence
- [ ] Mission generation
- [ ] Death screen and respawn

### Milestone 10 — Polish
**Depends on:** Milestone 9

- [ ] Title screen (rotating Cobra III, press any key)
- [ ] Commander save/load via VFS file
- [ ] Performance profiling and optimization
- [ ] Sound tuning: adjustEnvelope timings, explosion texture, engine pitch curve
- [ ] Thorough testing

## BBC Micro Screen Authenticity

The original BBC Micro screen mode was:
- **Mode 1**: 320x256, 4 colors (simultaneous)
- **Mode 2**: 160x256, 8 colors used for the space view
- **Mode 7** (Teletext): 40x25 text for the dashboard (in some versions)
- Split screen at raster line 256 via timer

Total addressable: 320x256 pixels in the space view.
Dashboard used the same Mode 1 text characters for bar indicators.

For our homage, the spirit is:
- **Wireframe ships on a solid black background** — the iconic look
- **Split screen with dashboard** — authentic division
- **256-color palette** — Mode 13h gives 256 programmable colors via VGA DAC;
  we use a curated 64-color subset that evokes the BBC Micro's 4/8-color modes
  with room for UI chrome, planet variety, and HUD gradients
- **Monospace font** — the dashboard uses bitmap font rendering
- **Green phosphor text** — for the dashboard, use VGA_COLOR_GREEN
  as the default text color

## Color Mapping

Mode 13h uses palette indices (0-255). We define a curated palette at game
init via VGA DAC ports (0x3C8/0x3C9). The table below shows the palette
indices and their RGB values:

| Element | Palette Index | RGB Value | BBC Micro Equivalent |
|---------|--------------|-----------|---------------------|
| Background (space) | 0 | 0x000000 | Black |
| Dashboard text | 2 | 0x00AA00 | Green (mode 1) |
| Ship wireframe | 7 | 0xAAAAAA | White |
| HUD bars (high) | 2 | 0x00AA00 | Green |
| HUD bars (low) | 4 | 0xAA0000 | Red |
| Sun (surface) | 6 | 0xAAAA00 | Yellow |
| Sun (corona) | 4 | 0xAA0000 | Red |
| Stardust | 7/3 | 0xAAAAAA/0x00AAAA | White/Cyan |
| Laser beam | 4 | 0xAA0000 | Red |
| Scanner ellipse | 2 | 0x00AA00 | Green |
| Hyperspace | 3 | 0x00AAAA | Cyan |
| Explosion flash | 5 | 0xAA00AA | Magenta |
| UI chrome (8-63) | 8-63 | various | Gradients, planet terrain |

When porting to VBE 24-bit later, the same palette indices map to the same
RGB values — only the output mechanism changes (DAC index vs. direct RGB).

Indices 64-255 are unused. Planet surfaces use indices 8-15 for terrain
variation (browns, greys, reddish).

## Reference Documents

### Elite-A Source Files
- Flight code: `1-source-files/main-sources/elite-source-flight.asm`
- Docked code: `1-source-files/main-sources/elite-source-docked.asm`
- Encyclopedia: `1-source-files/main-sources/elite-source-encyclopedia.asm`
- 6502SP parasite: `1-source-files/main-sources/elite-6502sp-parasite.asm`
- Build options: `1-source-files/main-sources/elite-build-options.asm`

### Deep Dive Articles (elite.bbcelite.com)
- Drawing ships (the LL9 routine, 12 parts)
- Flicker-free ship drawing (the LSPUT algorithm)
- Backporting the flicker-free algorithm
- Back-face culling (dot product method)
- Ship blueprints and ship data blocks
- Stardust routine
- Drawing circles (planet crater, meridian, equator)
- The sun (SUNS)
- Market item prices and availability
- Galaxy and system seeds (procedural generation)
- The local bubble of universe
- Orientation vectors (nosev, roofv, sidev)
- The main game loop and flight loop
- The ball line heap

## Flicker-Free Algorithm Reference

From the `flicker-free` branch in the Elite-A repo:

```
Initialization:
  LSNUM  = pointer to start of ship line heap
  LSNUM2 = pointer to end of old ship line heap (0 if no old ship)

LSPUT (draw one line with interleave):
  1. Calculate new line screen coords
  2. Draw new line to backbuffer (via libgfx draw_line)
  3. If LSNUM < LSNUM2:
       a. Fetch old line at [LSNUM] from ship line heap
       b. Draw old line in background color (erase)
       c. Store new line at [LSNUM] in ship line heap
  4. Increment LSNUM by 4 (next line slot)

LL9 Part 12 (modified):
  For each visible edge:
    Calculate edge endpoint screen coords
    Call LSPUT to draw with flicker-free interleave
  After all edges:
    While LSNUM < LSNUM2:
      Erase remaining old lines (ship shrank)
      Increment LSNUM
```
