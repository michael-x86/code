# Review x86cpu.js Page Fault Issues

You are tasked with reviewing the x86 CPU emulator in
`/home/janko/dev/code/webulator/hardemu/x86cpu.js` to resolve Page Fault (PF, exception 14)
bugs.

The project is a browser-based x86 emulator ("webulator") that boots a real OS kernel. Paging is
enabled when CR0.PG (bit 31) is set. The `translateAddress` method at line 305 handles virtual
→ physical translation and triggers #PF (exception 14) when PDE or PTE is not present (bit 0
clear).

## Known/observed PF issues to investigate

1. **PDE/PTE lookup during exception delivery** — When `triggerException()` reads the IDT entry
   (line 457), it calls `readMem()` which goes through `translateAddress()`. If the IDT itself
   is in a non-identity-mapped page, this recursive paging could cause double faults or infinite
   loops. Verify the IDT access path handles this correctly.

2. **`faultEip` correctness** — `faultEip` is set at line 547 before decoding, but memory
   accesses during prefix handling (lines 562-568) or operand fetching could already trigger a
   #PF. The pushed EIP should point to the instruction that caused the fault, not past it.
   Check that `faultEip` is not advanced past the instruction start before the PF occurs.

3. **Missing page-level protection checks** — `translateAddress` checks Present bit (bit 0) but
   does NOT check:
   - R/W violation (bit 1): write to a read-only page should #PF with error code bit 1 set
   - U/S violation (bit 2): user-mode access to supervisor page should #PF with error code bit 0 set
   - Reserved bit violations (bit 8+ in PDE for PSE, etc.)
   The error code pushed for #PF is always `0` (line 373, 398) instead of encoding the fault type.

4. **Recursive PF during exception delivery** — If `triggerException` itself triggers a page
   fault (e.g., IDT entry address is unmapped, or pushing to stack causes a PF), the CPU should
   either deliver a double fault (#DF, exception 8) or triple fault (reset). Currently this
   likely loops or halts.

5. **`cr2` not set for all PF cases** — CR2 is set at lines 372 and 397 for not-present faults,
   but should also be set for protection violations (R/W, U/S) if those were checked.

6. **No distinction between PF from instruction fetch vs data access** — Error code bit 4
   indicates whether the fault was caused by an instruction fetch. This is never set.

7. **Large pages (4MB PSE)** — The CPU enters `translateAddress` for any address without
   checking PS bit (bit 7) in the PDE which would indicate a 4MB page. This means 4MB-page
   mappings will be misinterpreted (the PDE frame address will be used directly as a page table
   base).

8. **Read of `pde` from memory uses physical address** — Line 331 does
   `this.mem.read32(pdeAddr)` directly (physical memory) rather than going through `readMem`,
   which is correct since CR3 holds a physical address. But verify that the page table walk
   always uses physical memory and never goes through paging recursively.

## Files to study

- `/home/janko/dev/code/webulator/hardemu/x86cpu.js` (3871 lines) — main CPU emulator
- `/home/janko/dev/code/webulator/hardemu/memory.js` (284 lines) — physical memory subsystem
- `/home/janko/dev/code/webulator/hardemu/machine_x86.js` — machine integration
- `/home/janko/dev/code/webulator/test.js` — existing tests (see page fault tests around line 1580)

## Task

1. Analyze the complete paging pathway: `readMem`/`writeMem` → `translateAddress` → physical
   memory. Check every call site for correctness.
2. Analyze the exception delivery pathway in `triggerException` for PF safety.
3. Implement fixes for all identified issues.
4. Ensure PF error codes are correctly constructed per the x86 architecture.
5. Verify by running the existing tests (the test runner is located in `test.js`).

Return a detailed report of all issues found, the fixes applied, and any remaining concerns.
