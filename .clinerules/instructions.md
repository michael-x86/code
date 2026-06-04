# x86 Assembly Kernel — Development Instructions

**Last updated**: 2026-06-04

> **Context-First Development**: Use repository documentation in `docs/` as the primary source of truth. Follow path-specific rules in `.clinerules/*.md`. Use the local code index (`build/code-index/code-index.sqlite`) for repo-wide file queries.

## Instruction Hierarchy

1. Path-specific rules in `.clinerules/*.md` for files you are editing
2. This file for project-wide defaults
3. Documentation under `docs/` as design and operational source-of-truth
4. `build/kernel.lst` (assembly listing) for actual addresses/opcodes
5. BIOS / hardware manuals (Intel SDM, AMD APM) for behaviour outside this repo

## Quick Reference

- **Language**: NASM x86 (32-bit, flat binary, no libc, no runtime)
- **Build**: `./asm` (delegates to `build/asm`; build artifacts stay in `build/`)
- **Build only**: `./asm`
- **Run in QEMU**: `./asm -r` (windowed)
- **Fullscreen**: `./asm -f`
- **GDB debug**: `./asm -d` (server on `localhost:1234`, halts at start)
- **Code index**: `./asm -i` (writes `build/code-index/code-index.sqlite`)
- **Assemble is the lint**: any NASM warning is a stop-the-line defect
- **CI**: there is none — manual verification per "Required Before PR" below
- **Repository context**: query the local code index for file/symbol lookups unless the exact path and line range are already known

## Core Principles

### Production Mandate (Non-Negotiable)

- **No stubs, prototypes, demos, or shortcuts** in code shipped to `main`
- **Complete implementations** with documented register/state contracts
- **No ring-0 handwaves**: every privileged path must be reasoned through
- **Real hardware compatibility** for firmware modules (PIC, PIT, PS/2, ATA, VGA) — QEMU validation is necessary but not sufficient
- **Validated by booting in QEMU** (clean prompt, exercised path, clean exit) before merging

### x86 Architecture Compliance (Non-Negotiable)

- **Official Intel SDM / AMD APM behaviour is mandatory** across bootloader, kernel, userspace, tools, and tests.
- **No exceptions are allowed.**
- Any deviation (illegal segment descriptor, misaligned stack, non-canonical address, gate-DPL mismatch, IF mishandling across ring transitions) is treated as **faulty code** and must be fixed before merge.
- Do not justify deviations by performance, schedule pressure, "it works in QEMU", or legacy behaviour.
- If unsafe behaviour is required at hardware/ABI boundaries (real-mode BIOS calls, ATA PIO, port I/O, ring transitions), it must be **narrowly scoped, documented with preconditions, and validated**.

### Development Workflow

When working on changes:

1. **Understand context**: read the relevant `docs/*.md` file before opening source files
2. **Follow structure**: keep code organized per project layout; section headers required
3. **Test thoroughly**: assemble, boot, exercise the affected path
4. **Document changes**: update `docs/*.md` and bump the timestamp at the top
5. **Validate**: NASM clean, QEMU clean prompt, affected commands work, clean exit

### Intent Clarification Rule

If the user's intent is materially ambiguous, do not guess.

Required behavior:

1. Ask for clarification when two or more reasonable interpretations would
   change scope, touched surfaces, validation burden, or whether the work is
   exploratory versus implementation.
2. Prefer one short clarification question over speculative implementation.
3. If helpful, present a small bounded set of interpretations and ask the user
   to choose.
4. Do not infer destructive, repo-wide, or policy-changing intent from brief
   shorthand unless the user confirms it.

Default stance:

- Clarify intent before acting when ambiguity would materially change the work.
- Only proceed without asking when the intended outcome is clear from the
  request and surrounding context.

### Major Change Request Discipline

When a user asks for a broad or potentially destructive change to source, build flow, disk layout, syscall ABI, or repository topology, do **not** treat the request as implicitly approved just because it is technically possible.

Required behavior before implementation:

1. Identify that the request is a **major change** when it implies any of the following:
   - mass migration or deletion of many files (e.g. dropping the `commands/` userland tree),
   - cross-cutting rewrites touching multiple subsystems (e.g. swapping the VFS layout),
   - replacing core tooling/workflows (the `asm` build script, `gen_fs.py`, the persistence scheme),
   - changes that can strand historical docs, tasks, or persisted user files,
   - irreversible cleanup framed as "delete all", "rewrite everything", or similar.
2. Pause and give the user the full picture first:
   - affected surfaces,
   - likely follow-on work,
   - validation burden (which `commands/*.asm` will break, which persisted slots are at risk),
   - operational risk (e.g. persistence region collisions with future FS growth),
   - what is in scope vs what only appears adjacent.
3. Offer a smaller staged alternative when the blast radius is large.
4. Ask for confirmation on the bounded tranche before proceeding if the original request appears under-specified or not fully thought through.

Default stance:

- Prefer tranche-based execution over repo-wide rewrites.
- Prefer making hidden coupling explicit before changing it.
- Treat "major cleanup" requests as planning-sensitive, not merely editing tasks.

### Runtime Iteration Cadence (General Rule)

Use a two-lane validation cadence to reduce iteration latency while preserving release confidence:

1. **Fast lane (every micro-iteration)**: Run `./asm` (assemble only). Should take <10s.
2. **Full lane (per tranche / before handoff)**: Run `./asm -r` (boot in QEMU, exercise affected path). Should take <2s of QEMU time.

Debug lane (only when something is broken):

3. **Debug lane**: `./asm -d` (QEMU halts, GDB attaches at `localhost:1234`).

## Project Structure

```
/
├── asm                          # thin wrapper → build/asm
├── setup.sh                     # workspace setup + dep check
├── bootloader.asm               # (original/) archived 16→32-bit boot stub
├── kernel/                      # kernel sources
│   ├── bootloader.asm           # BIOS → 32-bit protected mode
│   └── src/
│       ├── kernel.asm           # main kernel entry
│       └── includes/            # .inc split by subsystem
│           ├── constants.inc    # segment selectors, ports, addresses
│           ├── data.inc         # initialized globals
│           ├── bss.inc          # uninitialized globals
│           ├── paging.inc       # page table setup
│           ├── memory.inc       # physical frame allocator
│           ├── interrupt.inc    # IDT, IRQ handlers
│           ├── syscall.inc      # int 0x80 dispatch
│           ├── graphics.inc     # VGA Mode 13h + PS/2 mouse
│           ├── vfs.inc          # virtual filesystem core
│           ├── shell.inc        # VGA shell, line editor, command dispatch
│           ├── task.inc         # round-robin task switching
│           └── exec.inc         # userland loading
├── commands/                    # userland program sources (*.asm)
│   ├── pwd.asm ls.asm cd.asm cat.asm touch.asm write.asm rm.asm
│   ├── mkdir.asm rmdir.asm cp.asm mv.asm vi.asm ping.asm
│   ├── alloc.asm dealloc.asm peek.asm poke.asm dump.asm
│   ├── ps.asm exit.asm help.asm calc.asm argtest.asm
│   └── (games) gdemo.asm elite.asm enigma.asm invaders.asm zork.asm
├── build/                       # all build artifacts (gitignored)
│   ├── asm                      # the actual build logic — edit this, not ./
│   ├── gen_fs.py                # walks content dirs → fs.inc
│   ├── os.img                   # final disk image
│   ├── kernel.bin               # raw kernel binary
│   ├── kernel.lst               # assembly listing (addresses + opcodes)
│   ├── fs.inc                   # generated filesystem include
│   ├── bin/                     # compiled userland mirrors
│   ├── code-index/              # local code index (SQLite)
│   ├── q*.py                    # QEMU scripting helpers (qshot, qkeys, …)
│   └── proc/ var/log/ etc/      # content mirrored into the kernel FS
├── scripts/                     # project-level scripts
│   ├── code-index-build.py      # the local code indexer
│   └── lib/
├── docs/                        # structured documentation
│   ├── index.md
│   ├── getting-started.md
│   ├── architecture.md
│   ├── syscalls.md
│   ├── graphics.md
│   ├── filesystem.md
│   ├── userland.md
│   ├── abi-contract.md
│   ├── debugging.md
│   ├── hardware-porting.md
│   ├── contributing.md
│   ├── bootsector-expansion.md
│   └── elite-plan.md
├── original/                    # archived pre-split sources
│   ├── bootloader.asm
│   └── kernel.asm
└── .clinerules/                 # this directory (agent rules)
    ├── instructions.md          # main project-wide rules (this file)
    ├── kernel.md                # kernel/* rules
    ├── commands.md              # commands/* rules
    ├── syscalls.md              # int 0x80 ABI rules
    ├── build.md                 # build pipeline + code index rules
    ├── debugging.md             # GDB + QEMU workflow
    ├── graphics.md              # VGA Mode 13h rules
    ├── ai-context/              # agent handoffs (JSONL)
    ├── ISSUE_TEMPLATE/
    └── pull_request_template.md
```

## Required Before PR (Mirrors Manual Verification)

Run these checks before submitting any PR. There is no CI — these are the contract.

- [ ] **Build**: `./asm` produces no NASM warnings, no errors
- [ ] **Run**: `./asm -r` boots to a clean prompt, affected commands work, clean exit
- [ ] **QEMU clean**: no triple fault, no hang, no garbage on screen
- [ ] **Filesystem**: any runtime-created file you touched survives `./asm` rebuild (persistence region is in scope)
- [ ] **Docs**: update the relevant `docs/*.md` and bump the "Last updated" stamp
- [ ] **Verify**: no `build/os.img`, `build/kernel.bin`, `build/*.bin`, or other generated artifacts are committed
- [ ] **Userland**: any new `commands/<name>.asm` is also added to the `COMMANDS=(...)` array in `build/asm`
- [ ] **Syscall ABI**: any new syscall number is added to the dispatch table, declared in `docs/syscalls.md`, and exercised by a test caller
- [ ] **Code index**: rerun `./asm -i` if you added or moved any source file under `kernel/`, `kernel/src/includes/`, or `commands/`

If any step fails, fix and re-run. **Do not skip checks.**

## Delegated Detailed Standards

For file-level standards, use path-specific rules:

- Kernel sources (bootloader, kernel, includes): `.clinerules/kernel.md`
- Userland programs (`commands/*.asm`): `.clinerules/commands.md`
- System call ABI (adding/modifying int 0x80): `.clinerules/syscalls.md`
- Build pipeline, disk image, code index, QEMU helpers: `.clinerules/build.md`
- Debugging (GDB, QEMU monitor, troubleshooting): `.clinerules/debugging.md`
- VGA Mode 13h graphics engine and graphical programs: `.clinerules/graphics.md`

This file stays focused on cross-cutting workflow, validation gates, and agent bootstrap.

## Debugging

There are no kernel feature flags (no Cargo, no kernel config). The "knobs" are: which commands are built (the `COMMANDS=(...)` array in `build/asm`), and the QEMU launch flags.

### Quick Reference

| Goal                    | Command                                     | Notes                                  |
|-------------------------|---------------------------------------------|----------------------------------------|
| Assemble only           | `./asm`                                     | <10s; no QEMU                          |
| Boot to prompt          | `./asm -r`                                  | clean exit via `exit` command          |
| Fullscreen boot         | `./asm -f`                                  | `Ctrl+Alt+F` to release cursor         |
| GDB attach (halts)      | `./asm -d`                                  | GDB server on `localhost:1234`         |
| Rebuild code index      | `./asm -i`                                  | only when source layout changes        |
| Capture QEMU output     | `./asm -r 2>&1 \| tee run.log`              | for offline inspection                 |
| QEMU monitor            | edit `build/asm` to add `-monitor stdio`    | then `info registers`, `xp /Nxb addr`   |

### Common Debug Workflows

```bash
# Build + boot, exercise affected path
./asm -r 2>&1 | tee run.log
# In QEMU window: type commands, watch for hangs, "command not found", garbage

# Step through kernel
./asm -d
# In another terminal:
gdb
(gdb) target remote localhost:1234
(gdb) set architecture i386
(gdb) break *0xC0100000
(gdb) continue
(gdb) info registers
(gdb) stepi

# Diagnose a triple fault (boot hangs with no output)
./asm -d
(gdb) break *0x7C00
(gdb) continue
(gdb) stepi  # walk bootloader, watch for page fault on jump
# Compare build/kernel.lst against your .asm source for the suspected routine

# Inspect runtime state
./asm -r 2>&1 | tee run.log
# In QEMU, run `dump`, `regs`, `stack`, `heap` shell commands
```

### Debugging Resources

- **Complete guide**: [docs/debugging.md](../docs/debugging.md)
- **GDB primer + breakpoints**: [docs/debugging.md](../docs/debugging.md#gdb)
- **QEMU monitor commands**: [docs/debugging.md](../docs/debugging.md#qemu-monitor)
- **Troubleshooting table**: [docs/debugging.md](../docs/debugging.md#troubleshooting)

## Reference Pointers

### Key Files

- [README.md](../README.md) — project overview, syscall table, layout
- [asm](../asm) — build wrapper
- [build/asm](../build/asm) — actual build logic (edit this, not `./asm`)
- [kernel/bootloader.asm](../kernel/bootloader.asm) — BIOS → protected mode
- [kernel/src/kernel.asm](../kernel/src/kernel.asm) — kernel entry
- [build/gen_fs.py](../build/gen_fs.py) — content → fs.inc
- [docs/index.md](../docs/index.md) — documentation entry point

### Important Commands

- `./asm` — assemble only
- `./asm -r` — assemble + run in QEMU
- `./asm -f` — assemble + run fullscreen
- `./asm -d` — assemble + QEMU with GDB server
- `./asm -i` — build the local code index
- See [docs/getting-started.md](../docs/getting-started.md) for first-time setup

### Documentation

- [docs/index.md](../docs/index.md) — documentation map
- [docs/architecture.md](../docs/architecture.md) — boot, memory, subsystems
- [docs/syscalls.md](../docs/syscalls.md) — int 0x80 reference
- [docs/graphics.md](../docs/graphics.md) — VGA Mode 13h
- [docs/filesystem.md](../docs/filesystem.md) — VFS layout + persistence
- [docs/userland.md](../docs/userland.md) — writing/adding programs
- [docs/abi-contract.md](../docs/abi-contract.md) — position-independent binary rules
- [docs/debugging.md](../docs/debugging.md) — GDB, QEMU, troubleshooting
- [docs/hardware-porting.md](../docs/hardware-porting.md) — real-hardware gaps
- [docs/contributing.md](../docs/contributing.md) — code style, PR process
- [docs/elite-plan.md](../docs/elite-plan.md) — BBC Micro Elite port plan

## For Cline and Automated Agents

### Documentation Discovery (Mandatory)

Before implementation, discover documentation via the local code index and `docs/`. Do not read broad directory listings, whole long files, or large search results before asking the index for a targeted file, symbol, text, summary, window, or assembled-context result.

1. Build or refresh the index when missing or after source-layout changes:
   `./asm -i` (writes `build/code-index/code-index.sqlite`).
2. Query by lookup type (see "Local Code Index Workflow" below for exact flags).
3. Use `--summarize-path <path> --summary-query "<focus>"` for long docs
   instead of loading the whole file.
4. Open only the specific files and line ranges the index returns.

Do not edit scoped code until the relevant `docs/*.md` is reviewed.

### Instruction Bootstrap List (Mandatory)

Consult these before editing scoped paths:

- `.clinerules/kernel.md` — kernel sources
- `.clinerules/commands.md` — userland programs
- `.clinerules/syscalls.md` — int 0x80 ABI
- `.clinerules/build.md` — build pipeline + code index
- `.clinerules/debugging.md` — GDB + QEMU workflow
- `.clinerules/graphics.md` — VGA Mode 13h

When working as an automated assistant:

1. Query repository context through the code index before opening repo files, except when an exact path and line range are already known
2. Read `docs/index.md` and follow links to the area you are touching
3. Apply path-specific rules from `.clinerules/`
4. Use `./asm` for builds and `./asm -r` for validation; never edit `build/` artifacts by hand
5. Cite the source `docs/*.md` (or `docs/abi-contract.md`, `docs/syscalls.md`, etc.) in any commit or PR description that proposes a behaviour change
6. Keep documentation in sync with code changes (bump "Last updated")
7. Validate before submitting: `./asm` (no warnings) + `./asm -r` (clean boot, affected commands work)
8. Seek clarification when uncertain (see Intent Clarification Rule above)
9. Update `.clinerules/ai-context/handoffs.jsonl` (or `.clinerules/ai-context/session-deltas.jsonl`) with the structured session summary; prefer writing through the next agent's pickup workflow
10. Keep commands to 60-second timeouts; split longer runs into shorter bounded checks
11. Enforce `docs/05-roadmap`-style governance (we don't have one yet — call out missing planning docs when relevant)
12. Call out missing core implementations when encountered in code (e.g. "INT 0x80 syscall N exists in source but is undocumented in `docs/syscalls.md`")
13. Include security notes when touching ring-0 paths, the IDT, the syscall gate, the page tables, or the ATA PIO path

### Build/Test Log Budgeting (Mandatory)

For assemble/boot diagnosis and validation runs where live streaming is not required:

1. `./asm` and `./asm -r` are inherently short; full output is fine. If output gets long, redirect to a file under `build/` (e.g. `build/run.log`) and inspect with `rg`/the index.
2. Use `./asm -i` to refresh the code index after any source-layout change.
3. For long debug sessions, use GDB scripted breakpoints rather than streaming every step; keep transcripts under `build/debug-logs/`.
4. Do not ingest whole QEMU/GDB transcripts into context — narrow to the suspected routine by address (from `build/kernel.lst`) and read just that window.

### Macro Triggers

Treat the following user message prefixes as shorthand directives. Execute the mapped workflow directly unless the user adds extra constraints.

- `!build`
  - Run `./asm` (assemble only), report pass/fail and any warnings.
- `!run`
  - Run `./asm -r`, confirm clean boot, report prompt state.
- `!fullscreen`
  - Run `./asm -f`.
- `!debug`
  - Run `./asm -d`, remind user of the GDB attach steps (`target remote localhost:1234`, `set architecture i386`, `continue`).
- `!index`
  - Run `./asm -i` to (re)build the code index; report index path and size.
- `!pickup`
  - Read `.clinerules/instructions.md` first, then `.clinerules/ai-context/manifest.json` for the latest record pointers, then the first line of `.clinerules/ai-context/handoffs.jsonl` (most recent handoff). Summarize current status in 3-6 bullets and continue from the documented next step.
- `!write-handoff`
  - Compose a body markdown summarizing the session and write it to a temp file. The body should be bulleted for `summaryLines` extraction and include a `## Next Steps` section for `nextSteps` extraction. Then run:
    ```
    python3 scripts/ai/record-agent-context.py \
      --record-type handoff_snapshot \
      --title "<short-title-with-!trigger-if-applicable>" \
      --body-file /tmp/handoff-body.md \
      --frontier-note "<frontier observation>" \
      --next-step "<concrete next action>"
    ```
    The script upserts the record into `.clinerules/ai-context/handoffs.jsonl`, refreshes `.clinerules/ai-context/manifest.json`, and rewrites `.clinerules/AI-CONTEXT.md`. New records use the rugged-laptop schema (`bodyMarkdown` / `summaryLines` / `nextSteps` / `frontierBugIds` / etc.); existing code/ schema entries (the two `2026-06-*` records) are left as-is.
- `!handoff`
  - Alias for `!write-handoff`.
- `!resume`
  - Same as `!pickup`: read `.clinerules/ai-context/manifest.json` + the first line of `.clinerules/ai-context/handoffs.jsonl`, summarize in 3-6 bullets, continue from the documented next step.

Trigger handling rules:

- If a trigger appears with extra text, treat that text as additional
  requirements.
- If multiple triggers appear in one message, execute them in order.
- If a trigger conflicts with repository safety rules (e.g. "no shortcuts in main"),
  follow safety rules and report the conflict briefly.

Additional troubleshooting workflow:

- Before treating a regression as novel, query `.clinerules/ai-context/handoffs.jsonl` for similar `tags` (e.g. `qemu`, `ata`, `vfs`, `syscall`, `paging`).

### Skills to Apply

- **Build system**: Use `./asm` for all builds; never invoke `nasm` directly when you could `./asm` instead. Edit `build/asm`, not `./asm`.
- **Testing**: There is no automated test runner; `./asm -r` and a brief manual smoke test of the affected path is the contract.
- **Code generation**: Follow NASM idioms, the project code style, and the section-header convention. See `.clinerules/kernel.md` and `.clinerules/commands.md`.
- **Documentation**: Update `docs/` proactively, bump the "Last updated" stamp at the top of each touched file.
- **Code review**: Check for x86 correctness (segment limits, paging invariants, gate DPL, ring transitions), position-independent correctness in userland, and ABI compliance.
- **Refactoring**: Preserve behaviour — boot must still reach the prompt.
- **Debugging**: Use the GDB + QEMU workflow in `.clinerules/debugging.md`. Do not guess at addresses; consult `build/kernel.lst`.

### Local Code Index Workflow (Mandatory for Repository File Queries)

When querying information about files in this repository, use the local code index first to reduce irrelevant context. Do not load broad directory listings, whole long files, or large search results before asking the index for a targeted file, symbol, text, summary, window, or assembled-context result.

- Use a two-phase flow: discovery first, editing second.
  - Discovery: index queries and narrow windows only.
  - Editing: open only the selected files/sections needed for the change.
- Build or refresh the index when missing or after file changes:
  - `./asm -i` (or `scripts/code-index-build.py --root . --out build/code-index/code-index.sqlite`)
- Query by lookup type (the indexer's CLI is the canonical reference — start with `--help`):
  - text search:   `--text "<term>" --limit 20`
  - symbol lookup: `--symbol "<symbol>"`
  - path filter:   `--path "<path-substring>"`
  - retrieve:      `--retrieve "<conceptual query>" --top-k 5 --retrieval-budget 1200`
  - assemble:      `--assemble-context "<task query>" --task-type analysis --output-budget 1800`
  - summarize:     `--summarize-path "<path>" --summary-query "<focus>"`
  - window:        `--window-path "<path>" --window-query "<focus>" --window-tokens 512 --overlap-tokens 128`
- Keep the index fresh:
  - Run `./asm -i` after any source-layout change.
  - Do not commit the index under `build/code-index/` (it is gitignored).

Escalation and context controls (mandatory):

- For files roughly >400 lines, do not read the whole file first. Start with one of:
  `--summarize-path`, `--window-path`, `--symbol`, `--text`, `--retrieve`, or `--assemble-context`.
- For conceptual retrieval, escalate budgets gradually: `600 -> 1200 -> 1800`.
  Do not jump directly to broad context unless earlier steps fail.
- Before opening more than 2 files or opening any large file (>400 lines), state a one-line justification for why broader context is required.
- After multi-file exploration, compress findings into a short working summary before continuing.
- Avoid repeated reformulations of the same retrieval query. After 2 attempts, inspect the best candidate files directly.
- Treat index output as a relevance map: open only selected files and line ranges after the index identifies them.
- Prefer `--retrieve` or `--assemble-context` for conceptual, cross-cutting, or multi-file requests.
- Prefer `--summarize-path` for long known files and `--window-path` for logs, generated text, or sequential content.
- Use direct file reads only for small known files, exact line ranges, or final verification after index narrowing.

---

**Last updated**: 2026-06-04
