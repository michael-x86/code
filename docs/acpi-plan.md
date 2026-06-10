# ACPI Support Implementation Plan

## Background & Motivation

The kernel currently has no standardized way to discover hardware or perform
power management. System shutdown goes through a QEMU-specific debug-exit hack
(port `0xF4` in `drivers/vga.drv:450`), reboot is not implemented at all, and
all hardware knowledge (ATA port base, PIC layout, memory size) is passed by the
bootloader through a fixed handoff struct at physical `0x500`.

**ACPI** (Advanced Configuration and Power Interface) is the PC-industry standard
for: platform device discovery, power-state management (sleep/off/reboot), multi-
processor configuration, and timer enumeration. Adding ACPI support lets the
kernel discover these features in a portable way across webulator, QEMU, and
eventually real hardware.

### What ACPI provides

| Feature | ACPI Table | What the kernel gains |
|---------|-----------|----------------------|
| Reboot | **FADT** Reset Register | Standardized port/MMIO write instead of magic values |
| Power-off | **FADT** SLP_TYPx + PM1a_CNT | ACPI S5 sleep state (system shutdown) |
| CPU topology | **MADT** LAPIC entries | SMP discovery (future) |
| APIC config | **MADT** LAPIC/IOAPIC entries | Replace 8259A PIC with APIC mode (future) |
| Timer discovery | **HPET** table | High-precision event timer (future) |
| Device tree | **DSDT** AML namespace | Full PCI/device enumeration (future) |
| NUMA topology | **SRAT** table | Memory affinity (future) |

### Current state (no ACPI)

- webulator has zero ACPI emulation — no RSDP, no tables, no AML
- The kernel has no ACPI parser — no RSDP scan, no table walker
- Shutdown is QEMU-only: `out 0xF4, 0` via `isa-debug-exit`
- Reboot is unimplemented
- All hardware topology is fixed at compile time or passed via bootloader handoff

---

## Phase 1 — ACPI Table Parser (kernel)

Create a new kernel module `drivers/acpi.drv` that scans physical memory for
the RSDP (Root System Description Pointer), walks the RSDT/XSDT, and exposes
individual table accessors.

### RSDP scan

- Search physical memory `0xE0000`–`0xFFFFF` for the 8-byte signature `"RSD PTR "`
- On match: verify the 1-byte checksum (all RSDP bytes sum to 0 mod 256)
- For ACPI v2.0+: verify the extended checksum too (XSDT case)
- Extract: RSDT address, XSDT address (v2.0+), revision

**Interface:**

```nasm
; acpi_init — scan for RSDP, locate RSDT/XSDT
; out:  CF = 0 on success (rsdt_found set), CF = 1 if no ACPI tables
acpi_init:
```

### RSDT/XSDT walker

- Read the RSDT (32-bit table pointer array) or XSDT (64-bit)
- Validate each table's signature and checksum (all table headers have a 1-byte
  checksum covering the entire table)
- Cache pointers to known tables: FADT, MADT, HPET, etc.

**Interface:**

```nasm
; acpi_find_table — locate an ACPI table by signature
;  in:  eax = 4-byte signature (e.g. "FADT" as a dword)
; out:  eax = physical address of table, or 0 if not found
acpi_find_table:
```

### FADT parser

Parse the Fixed ACPI Description Table (116 bytes minimum) for:

| Field | Offset | Use |
|-------|--------|-----|
| `facs` | 36 | Firmware ACPI Control Structure (unused initially) |
| `dsdt` | 40 | DSDT address (for AML parsing, Phase 4+) |
| `pm1a_evt_blk` | 48 | PM1a event register block (sleep status) |
| `pm1a_cnt_blk` | 60 | PM1a control register block (write SLP_TYPx here) |
| `pm1b_cnt_blk` | 64 | PM1b control register (secondary, optional) |
| `pm_tmr_blk` | 56 | PM timer (optional, for HPET-free timing) |
| `reset_reg` | 100 | **Reset register** (AddressSpaceId, Address, Value) |
| `reset_value` | 108 | Value to write to reset_reg to trigger reset |
| `smi_cmd` | 32 | SMI command port (unused without SMM) |
| `acpi_enable` | 36 | Value to write to SMI_CMD to enable ACPI |
| `acpi_disable` | 37 | Value to disable ACPI |
| `s4bios_req` | 38 | Value for S4BIOS support |
| `pstate_cnt` | 40 | Processor performance state control |
| `c2_latency` | 44 | Worst-case C2 latency (µs) |
| `c3_latency` | 46 | Worst-case C3 latency (µs) |
| `sci_int` | 16 | System Control Interrupt number |

### MADT parser

Parse the Multiple APIC Description Table for:

- **Local APIC entries**: CPU processor ID + APIC ID + flags (enabled bit)
- **I/O APIC entries**: APIC ID + physical address + global system interrupt base
- Used to build a CPU inventory and APIC routing table

### Graceful fallback

If no RSDP is found (as in current webulator), fall back to existing behaviour:
- `acpi_is_present()` returns false
- `acpi_reboot()` falls through to the QEMU port 0xF4 method
- `acpi_shutdown()` falls through to the same

### Files to create / modify

| File | Action | Description |
|------|--------|-------------|
| `kernel/src/drivers/acpi.drv` | **Create** | ACPI table parser (~400 lines asm) |
| `kernel/src/includes/constants.inc` | Modify | Add PM1a_CNT base, reset port, etc. |
| `kernel/src/drivers/vga.drv` | Modify | Replace shutdown with `acpi_shutdown()` |
| `kernel/src/kernel.asm` | Modify | Add `%include "drivers/acpi.drv"` |

### Key data structures

```nasm
; RSDP (Root System Description Pointer) — found at 0xE0000-0xFFFFF
struc RSDP
    .signature:    resb 8    ; "RSD PTR "
    .checksum:     resb 1    ; sum of all bytes = 0 mod 256
    .oem_id:       resb 6    ; OEM identifier
    .revision:     resb 1    ; 0=ACPI 1.0, 2=ACPI 2.0+
    .rsdt_addr:    resd 1    ; physical address of RSDT (32-bit)
    ; Fields below only valid for revision >= 2:
    .length:       resd 1    ; total RSDP length
    .xsdt_addr:    resq 1    ; physical address of XSDT (64-bit)
    .ext_checksum: resb 1    ; checksum of entire RSDP
endstruc

; SDT (System Description Table header) — first 36 bytes of every ACPI table
struc SDT
    .signature:    resb 4    ; table identifier
    .length:       resd 1    ; total table length incl. header
    .revision:     resb 1    ; revision
    .checksum:     resb 1    ; whole-table checksum
    .oem_id:       resb 6    ; OEM ID
    .oem_table_id: resb 8    ; OEM table ID
    .oem_revision: resd 1    ; OEM revision
    .creator_id:   resd 1    ; creator ID
    .creator_rev:  resd 1    ; creator revision
endstruc

; FADT (Fixed ACPI Description Table) — key fields
struc FADT
    .header:        resb 36   ; SDT header
    .facs:          resd 1    ; FACS physical addr
    .dsdt:          resd 1    ; DSDT physical addr
    .smi_cmd:       resb 1    ; SMI command port
    .acpi_enable:   resb 1
    .acpi_disable:  resb 1
    .s4bios_req:    resb 1
    .pstate_cnt:    resb 1
    .pm1a_evt_blk:  resd 1    ; PM1a event block base
    .pm1b_evt_blk:  resd 1
    .pm1a_cnt_blk:  resd 1    ; PM1a control block base
    .pm1b_cnt_blk:  resd 1
    .pm2_cnt_blk:   resd 1
    .pm_tmr_blk:    resd 1
    .gpe0_blk:      resd 1
    .gpe1_blk:      resd 1
    .pm1_evt_len:   resb 1
    .pm1_cnt_len:   resb 1
    .pm2_cnt_len:   resb 1
    .pm_tmr_len:    resb 1
    .gpe0_blk_len:  resb 1
    .gpe1_blk_len:  resb 1
    .gpe1_base:     resb 1
    .cst_cnt:       resb 1
    .c2_latency:    resw 1
    .c3_latency:    resw 1
    .flush_size:    resw 1
    .flush_stride:  resw 1
    .duty_offset:   resb 1
    .duty_width:    resb 1
    .day_alarm:     resb 1
    .month_alarm:   resb 1
    .century:       resb 1
    .iapc_boot_arch: resw 1   ; IAPC boot architecture flags
    .reserved:      resb 1
    .flags:         resd 1    ; fixed feature flags
    .reset_reg:     resb 12   ; Generic Address Structure (12 bytes)
    .reset_value:   resb 1    ; value to write to reset_reg
    .arm_boot_arch: resw 1
    .minor_version: resb 1
    .x_facs:        resq 1
    .x_dsdt:        resq 1
    .x_pm1a_evt_blk: resb 12
    .x_pm1b_evt_blk: resb 12
    .x_pm1a_cnt_blk: resb 12
    .x_pm1b_cnt_blk: resb 12
    .x_pm2_cnt_blk:  resb 12
    .x_pm_tmr_blk:   resb 12
    .x_gpe0_blk:     resb 12
    .x_gpe1_blk:     resb 12
endstruc
```

### Reboot via ACPI

```nasm
; acpi_reboot — trigger system reset via FADT reset register
; If ACPI is available: write reset_value to reset_reg address
; Otherwise: fall back to port 0x92 (keyboard controller reset)
acpi_reboot:
```

The FADT `reset_reg` is a Generic Address Structure (GAS, 12 bytes):
- `AddressSpaceId`: 0=system memory, 1=system I/O
- `RegisterBitWidth`: usually 8
- `RegisterBitOffset`: 0
- `Address`: port or MMIO address
- `AccessSize`: 0=undefined, 1=byte, etc.

**Typical value in QEMU:** I/O port `0xCF9`, write value `0x06`
(reset_value field). On real hardware, port `0xCF9` with value `0x0E`
(hard reset) is common.

### Power-off via ACPI (S5 sleep)

```nasm
; acpi_shutdown — enter ACPI S5 (soft-off) state
; Write (SLP_TYPa << 0) | (SLP_TYPb << 8) | SCI_EN to PM1a_CNT
; The SLP_TYPx values come from the DSDT's \_S5 object:
;   Package {0x05, 0x05, 0x00, 0x00} in QEMU's DSDT
; Default fallback: port 0xF4 (QEMU isa-debug-exit) or port 0x604
acpi_shutdown:
```

This requires either:
- Hardcoded SLP_TYPx values for known platforms
- A minimal AML parser that evaluates the `\_S5` object (Phase 4)

---

## Phase 2 — ACPI Table Provider (webulator)

For the kernel's ACPI parser to be testable, webulator must expose ACPI tables
in the standard scan region (`0xE0000`–`0xFFFFF`).

### New file: `hardemu/acpi.js`

Generate the following tables:

| Table | Signature | Required | Content |
|-------|-----------|----------|---------|
| RSDP | `"RSD PTR "` | Yes | At physical `0xF0000`, points to RSDT |
| RSDT | `"RSDT"` | Yes | 32-bit pointers to FADT, MADT (HPET optional) |
| FADT | `"FACP"` | Yes | Reset reg `0xCF9/0x06`, PM1a_CNT at `0x600`, DSDT pointer |
| DSDT | `"DSDT"` | Yes | Root scope `\_SB_` with `\_S5` sleep package |
| MADT | `"APIC"` | Yes | 1 LAPIC entry (current CPU, flags=1), 1 IOAPIC at `0xFEC00000` |
| HPET | `"HPET"` | No | Only if you add HPET emulation later |

### Memory layout

```
0x000F0000  +-------------------+
            | RSDP (36 bytes)   |  — 16-byte aligned
0x000F0020  +-------------------+
            | RSDT (36+4N bytes)|
0x000F0060  +-------------------+
            | padding           |
0x000F0100  +-------------------+
            | FADT (116-244 B)  |  — standard fixed length + extended
0x000F0180  +-------------------+
            | padding           |
0x000F0200  +-------------------+
            | MADT (varies)     |
0x000F0280  +-------------------+
            | padding           |
0x000F0300  +-------------------+
            | DSDT (~200 bytes) |
0x000F03C8  +-------------------+
```

All tables must be 4-byte aligned. The RSDP at `0xF0000` must be at an 8-byte
aligned (or 16-byte aligned for ACPI v2.0) address.

### RSDP structure

```javascript
// Physical 0xF0000: RSDP v1 (ACPI 1.0, 20 bytes)
// We use v1 for simplicity — the kernel detects revision and adapts.
const RSDP = new Uint8Array([
    0x52, 0x53, 0x44, 0x20, 0x50, 0x54, 0x52, 0x5F,  // "RSD PTR "
    0x00,  // checksum placeholder
    0x4F, 0x45, 0x4D, 0x58, 0x58, 0x58,              // OEMID = "OEMXXX"
    0x01,  // revision (ACPI 2.0+)
    0x00, 0x00, 0xF0, 0x0F,                           // RSDT address = 0x000F0020
    0x24, 0x00, 0x00, 0x00,                           // length = 36
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // XSDT = 0 (not used)
    0x00,  // extended checksum placeholder
]);
```

### FADT key values

| Field | Value | Notes |
|-------|-------|-------|
| `SCI_INT` | 9 | ISA IRQ9 for SCI (standard) |
| `SMI_CMD` | 0 | No SMM |
| `PM1a_EVT_BLK` | 0x600 | Legacy PM1a event port base |
| `PM1b_EVT_BLK` | 0 | Not present |
| `PM1a_CNT_BLK` | 0x604 | Legacy PM1a control port base |
| `PM1b_CNT_BLK` | 0 | Not present |
| `PM_TMR_BLK` | 0x608 | Legacy PM timer port base |
| `RESET_REG` | I/O port 0xCF9, 8-bit | Generic Address Structure |
| `RESET_VALUE` | 0x06 | Reset value |
| `IAPC_BOOT_ARCH` | 0x03 | Legacy devices: VGA + PS/2 present |
| `FLAGS` | bit 0 = WBINVD, bit 2 = SLPI | WBINVD + SLP button enabled |

### DSDT (minimal AML)

The DSDT must define `\_S5` so the kernel can determine the sleep type values.
In QEMU's standard DSDT:

```
\_S5 = Package(0x04) { 0x05, 0x05, 0x00, 0x00 }
```

The values `{0x05, 0x05, 0x00, 0x00}` mean:
- `SLP_TYPa` = 0x05 (bits 0-4 of PM1a_CNT)
- `SLP_TYPb` = 0x05 (bits 8-12 of PM1a_CNT)

**AML bytecode for `\_S5`:**

```javascript
// Minimal DSDT with just \_S5 definition
// AML bytes: Scope(\_SB) { Name(\_S5, Package(4){0x05,0x05,0x00,0x00}) }
const dsdt_aml = [
    0x10, 0x40 + 0x1C, 0x5F, 0x5F, 0x04, 0x5F, 0x53, 0x42, 0x5F,  // Scope(\_SB_)
    0x08, 0x5F, 0x53, 0x35, 0x5F,  // Name(\_S5
    0x12, 0x0C,                    // Package(4)
    0x04,                          // 4 elements
    0x0A, 0x05,                    // 0x05 (SLP_TYPa)
    0x0A, 0x05,                    // 0x05 (SLP_TYPb)
    0x00,                          // 0x00 (SLP_TYPc, reserved)
    0x00,                          // 0x00 (SLP_TYPd, reserved)
    // ... remaining Scope body
];
```

### MADT entries

The kernel currently uses the 8259A PIC (no local APIC, no I/O APIC). The MADT
should reflect this while laying groundwork for future APIC support:

```javascript
// MADT (Multiple APIC Description Table)
// Fixed header (44 bytes) + entries:
//   - Processor Local APIC: flags=1 (enabled)
//   - I/O APIC: base = 0xFEC00000
//   - 8259A override: flags=1 (8259A present, no override needed if
//     PIC is the primary interrupt controller)

const madt_entries = [
    // 1. Processor Local APIC entry (8 bytes)
    { type: 0, length: 8,
      processor_id: 0, apic_id: 0, flags: 1 },

    // 2. I/O APIC entry (12 bytes)
    { type: 1, length: 12,
      ioapic_id: 0, ioapic_addr: 0xFEC00000, gsi_base: 0 },

    // 3. 8259A source override entry (optional — only if PIC sends
    //    IRQ0-15 to the I/O APIC's GSIs)
    { type: 2, length: 10,
      bus: 0, irq: 0, gsi: 2, flags: 0x0000 },
];
```

### Integration into `machine_x86.js`

In the machine init routine, after initializing memory:

```javascript
// In X86Machine.init():
this.acpi = new ACPI(this.memory);
this.acpi.installTables();

// Add RSDP scan region to the memory map
this.memory.writeRange(0x000F0000, this.acpi.getRSDP());
this.memory.writeRange(0x000F0020, this.acpi.getRSDT());
this.memory.writeRange(0x000F0100, this.acpi.getFADT());
this.memory.writeRange(0x000F0200, this.acpi.getMADT());
this.memory.writeRange(0x000F0300, this.acpi.getDSDT());
```

### Files to create / modify

| File | Action | Description |
|------|--------|-------------|
| `webulator/hardemu/acpi.js` | **Create** | ACPI table generator (~350 lines JS) |
| `webulator/hardemu/machine_x86.js` | Modify | Call `acpi.installTables()` after memory init |
| `webulator/hardemu/memory.js` | Modify | Ensure 0xF0000-0xF0400 is readable RAM |

---

## Phase 3 — Integration & System Calls

Once Phase 1 and 2 are complete, wire ACPI into the kernel's main paths.

### Replace `shutdown` in `drivers/vga.drv`

```nasm
; Current (hardcoded QEMU hack):
shutdown:
    cli
    mov dx, 0xF4
    mov al, 0
    out dx, al
    hlt
    ret

; New (ACPI-aware):
acpi_shutdown_wrapper:
    cli
    call acpi_shutdown        ; try ACPI S5 first
    test eax, eax
    jz .done                  ; success — should never return
    ; fallback: QEMU isa-debug-exit
    mov dx, 0xF4
    mov al, 0
    out dx, al
    hlt
.done:
    ret
```

### Add `reboot` command

```nasm
; New helper, usable from shell or panic handler:
acpi_reboot_wrapper:
    cli
    call acpi_reboot          ; try ACPI reset reg first
    test eax, eax
    jz .done
    ; fallback: keyboard controller port 0x64 pulse
    mov al, 0xFE
    out 0x64, al
    hlt
.done:
    ret
```

### New syscall (optional)

| Syscall | Number | Description |
|---------|--------|-------------|
| `sys_reboot` | 48 | Trigger ACPI reboot |
| `sys_shutdown` | 49 | Trigger ACPI S5 shutdown |

Add entries to the syscall table in `data.inc` and update `SYSCALL_COUNT`.

### ACPI diagnostics

Add an `acpi` shell command that dumps:
- RSDP address and revision
- Number of RSDT entries found
- Whether FADT, MADT, DSDT, HPET are present
- FADT reset register details
- MADT: number of CPUs, I/O APIC count

---

## Phase 4 — Future (Long-Term)

### 4a. Minimal AML Interpreter

The DSDT uses ACPI Machine Language (AML) to describe devices. A full AML
interpreter is a significant project (~2000+ lines). For the immediate goal
of power-off, you can hardcode the `\_S5` values from known tables. For
full device enumeration, you need AML evaluation.

**Minimal AML features needed:**

| Feature | Opcode | Use |
|---------|--------|-----|
| Package | 0x12 | `\_S5` sleep package |
| Name | 0x08 | Named objects in namespace |
| Integer | 0x0A/0x0B/0x0C | Integer constants (Byte/Word/DWord) |
| Scope | 0x10 | Device scope `\_SB_` |
| Device | 0x5B 0x82 | Device declaration |
| Method | 0x14 | Control method |
| Return | 0xA4 | Return value |
| Store | 0x70 | Store to register |
| Field | 0x5B 0x81/0x82 | Register field definition |

### 4b. PCI Enumeration

- Use MCFG table for PCI Express Enhanced Configuration Access Mechanism (ECAM)
- Fall back to PCI BIOS INT 1Ah for legacy PCI
- Enumerate bus/dev/func, read vendor/device IDs, BARs
- Build a device tree in the kernel

### 4c. APIC Mode

- Parse MADT for local APIC + I/O APIC base addresses
- Program the local APIC (MSRs 0x1B, 0x1B1+) for interrupt delivery
- Remap IRQs from the 8259A PIC to the I/O APIC (requires disabling the PIC)
- Enable APIC timer (replaces PIT as the system tick)
- Foundation for SMP later

### 4d. HPET Driver

- Parse HPET table for:
  - Base address (typically MMIO at `0xFED00000`)
  - Periodic timer capability
  - Main counter 64-bit
- Compare with current PIT: HPET provides ~10× higher resolution
- Use for: wall clock, profiling, high-resolution waits

### 4e. SMP (Symmetric Multi-Processing)

Requires:
- webulator multi-core support (separate work)
- Local APIC + I/O APIC (Phase 4c above)
- MADT processor entries
- ACPI CPU hotplug AML methods (`\_SB_.CP00._MAT`, `\_SB_.CP00._STA`)
- Startup IPI (SIPI) protocol

---

## Estimated effort & dependencies

| Phase | Scope | Files | Lines | Depends on |
|-------|-------|-------|-------|------------|
| **P1** | ACPI table parser (kernel) | `drivers/acpi.drv`, `constants.inc` | ~400 asm | — |
| **P2** | ACPI table provider (webulator) | `acpi.js`, `machine_x86.js` | ~350 JS | — |
| **P3** | Integration (reboot/shutdown) | `vga.drv`, `data.inc`, `syscall.inc` | ~80 asm | P1 + P2 |
| **P4a** | AML interpreter | `acpi_aml.inc` | ~2000+ asm | P1 |
| **P4b** | PCI enumeration | `pci.drv`, `mcfg.drv` | ~500 asm | P1, P4a |
| **P4c** | APIC mode | `lapic.inc`, `ioapic.inc`, `pic.inc` | ~600 asm | P1 |
| **P4d** | HPET driver | `hpet.drv` | ~300 asm | P1 |
| **P4e** | SMP | `smp.inc`, `madt.inc` | ~800 asm | P4c |

**Direct path to reboot + shutdown:** P1 + P2 + P3 (~830 lines total).

**Full device + APIC + SMP path:** All phases (~5000 lines).

---

## Current recommendation

Implement **Phases 1–3** in sequence:

1. **Phase 1 first** — write `drivers/acpi.drv` with RSDP scan + RSDT walker +
   FADT parser. This can be tested against QEMU (which has full ACPI tables)
   even without webulator changes. QEMU provides ACPI out of the box.
2. **Phase 2 second** — add `hardemu/acpi.js` to webulator so the same kernel
   binary discovers ACPI in both emulators.
3. **Phase 3 third** — wire up reboot/shutdown and the `acpi` diagnostic command.

This gives reboot + power-off in both QEMU and webulator. Phase 4 (AML, APIC,
HPET, SMP) can be deferred until the kernel needs those features.

---

## Testing strategy

| Test | How | Expected result |
|------|-----|-----------------|
| `acpi_init` detects RSDP | Boot in QEMU + webulator | `rsdt_found` is non-zero |
| `acpi_find_table` walks RSDT | Call with "FACP", "APIC" | Returns non-null physical address |
| FADT reset register | Read `[fadt + reset_reg]` | Port 0xCF9, value 0x06 |
| `acpi_reboot` | Call from shell | System resets cleanly |
| `acpi_shutdown` | Call from shell | System powers off (QEMU: window closes) |
| No-ACPI fallback | Boot webulator before P2 | Shutdown still works via port 0xF4 |
| ACPI diag command | `acpi` at shell prompt | Prints RSDP addr, table presence |
