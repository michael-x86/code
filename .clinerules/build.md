---
paths:
 - "build/asm"
 - "build/gen_fs.py"
 - "build/q*.py"
 - "build/relocate_*.py"
 - "scripts/**"
---

# Build Pipeline — Rules

**Last updated**: 2026-06-04

Applies to: `build/`, `scripts/`, the `./asm` wrapper, and `build/gen_fs.py`. Read [docs/getting-started.md](../docs/getting-started.md) for the developer-facing overview.

## The Two Script Files

There are two build script entry points. Do not conflate them.

| File      | Role                                          | Edit?       |
|-----------|-----------------------------------------------|-------------|
| `./asm`   | Thin bash wrapper that `exec`s `build/asm`    | Almost never |
| `build/asm` | The actual build logic (assemble, fs gen, QEMU launch, code index) | Yes, when build behaviour changes |

When you need to change build behaviour, edit `build/asm`. The wrapper at `./asm` exists only to keep the project root clean and to make it easy to override the build directory.

## Build Artifacts (Stay in `build/`)

All generated files go under `build/`. The directory is gitignored. Never commit any of:

- `build/os.img` — final disk image
- `build/kernel.bin` — raw kernel binary
- `build/kernel.lst` — assembly listing (addresses + opcodes)
- `build/fs.inc` — generated filesystem include
- `build/bin/<name>` — compiled userland
- `build/boot.img` — bootloader image
- `build/fs.img` — filesystem image
- `build/code-index/` — SQLite index and WAL files
- `build/proc/`, `build/var/log/`, `build/etc/`, `build/usr/`, `build/dev/`, `build/lib/` — content mirrors

The only files under `build/` that should be tracked are:

- `build/asm` — the build script itself
- `build/gen_fs.py` — the fs generator
- `build/q*.py` — QEMU scripting helpers (qshot, qkeys, qdrive, qint, qmem, qsample, qstack, qstate, qtest_enigma, qtrace, qvga, etc.)
- `build/relocate_enigma.py` — special-purpose relocation helper
- `build/shot.png` — reference screenshot (committed if small; check `.gitignore`)

If you add a new build artifact, add it to `.gitignore` in the same commit.

## The Filesystem Generator (`build/gen_fs.py`)

`gen_fs.py` walks the kernel content directories (`kernel/etc/`, `kernel/proc/`, `kernel/var/log/`) and emits `build/fs.inc`, an assembly include embedded directly into the kernel image. The kernel reads the records at boot to populate the in-kernel VFS.

Rules:

- Do not edit `build/fs.inc` by hand. It is regenerated on every `./asm`.
- If you add a new content directory, update both `build/asm` (so the directory is created) and `gen_fs.py` (so the records are generated). Update [docs/filesystem.md](../docs/filesystem.md).
- The persistence region (spare slots at LBA `256 + N*3`) is **backed up before kernel reassembly and restored after** by `build/asm`. Do not change this behaviour without understanding the implications for runtime-created files.

## The Local Code Index (`./asm -i`)

`./asm -i` runs `scripts/code-index-build.py` and writes a SQLite index to `build/code-index/code-index.sqlite`. This is the project's equivalent of the rugged-laptop `omnix index` and is **mandatory for file/symbol lookups** in this repo (see "Local Code Index Workflow" in `.clinerules/instructions.md`).

Rules:

- Run `./asm -i` after any source-layout change (added/moved/renamed `.asm` or `.inc`).
- The index is gitignored under `build/code-index/`. Do not commit it.
- The indexer is incremental — running it when nothing has changed is fast (~1s). Running it after a large change takes longer; budget 30s for a full rebuild.
- Treat index output as a relevance map: open only the files and line ranges the index identifies, never the whole tree.
- If the indexer reports a parse error on a file, fix the file (NASM warnings are stop-the-line defects).

## QEMU Helpers (`build/q*.py`)

The `q*.py` scripts under `build/` are automation around QEMU for common tasks:

- `qshot.py` — screenshot
- `qkeys.py` — send keystrokes
- `qdrive.py` — drive geometry
- `qint.py`, `qmem.py`, `qstack.py`, `qstate.py`, `qtrace.py` — runtime state inspection
- `qvga.py` — VGA state
- `qdrive_enigma.py`, `qtest_enigma.py`, `relocate_enigma.py` — Enigma-specific helpers

When adding a new QEMU helper:

- Place it under `build/`, prefixed with `q` (e.g. `qsmoke.py`)
- Take a QEMU monitor socket or `-serial stdio` as input — do not require modifying `build/asm`
- Document the script's purpose in a header comment
- Add a one-line entry to [docs/debugging.md](../docs/debugging.md) if the script is reusable

## Disk Image Layout

```
[LBA 0]                bootloader (512 bytes, one sector)
[LBA 1..N]             kernel (raw, padded to sector boundary)
[LBA 256..256+16*3-1]  persistence region (16 spare slots, 3 sectors each)
```

The kernel image is loaded by the bootloader at boot time and copied to `0x100000`, then mapped to `0xC0100000` via paging. The persistence region is read by `load_fs_persist` and replayed into the in-kernel VFS spare slots.

Do not change the LBA layout without updating:

- The bootloader's `INT 13h` reads
- `gen_fs.py`'s backing-up/restoring of the persistence region
- [docs/filesystem.md](../docs/filesystem.md)
- Any test that creates a runtime file and asserts it survives a rebuild

## Adding a New Program to the Build

(See `.clinerules/commands.md` for the userland-side rules. The build-side steps are:)

1. Write `commands/<name>.asm` (per the userland contract)
2. Append `<name>` to the `COMMANDS=(...)` array in `build/asm` (alphabetic, with the others)
3. Run `./asm` — confirm `build/bin/<name>` is created and the size check passes (`< 65536` bytes)
4. Run `./asm -r` — boot, invoke `<name>`, confirm it works
5. Run `./asm -i` — refresh the code index

## Validation Gate

Before a build change ships:

1. `./asm` — clean, no warnings
2. `./asm -r` — clean boot
3. Run a representative program (`cat /etc/config`, `ls`, `ping`) to confirm the FS still works
4. If you touched the persistence layout: create a runtime file (`touch foo; write foo bar`), rebuild, reboot, confirm `foo` still exists with content `bar`
5. If you touched `gen_fs.py`: rebuild, confirm `build/fs.inc` is updated and the kernel still boots
6. If you touched the code indexer: run `./asm -i`, confirm a sample query returns the expected file
7. No `build/*` artifact is staged in `git add` output

## Anti-Patterns (Don't)

- Don't edit `./asm` — edit `build/asm`
- Don't edit `build/fs.inc` by hand
- Don't commit anything under `build/` except the build scripts themselves
- Don't change the LBA layout in a single commit without coordinating with the bootloader, `gen_fs.py`, and docs
- Don't add a "smart" wrapper around `nasm` that hides warnings — every warning is a defect, see it, fix it
- Don't skip `./asm -i` after a source-layout change

---

**Last updated**: 2026-06-04
