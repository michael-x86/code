/**
 * ACPI Table Provider
 *
 * Generates standard ACPI tables (RSDP, RSDT, FADT, MADT, DSDT) and
 * installs them in the emulated memory at physical 0xF0000-0xF0400,
 * which is the standard BIOS/E820 scan region for the RSDP.
 *
 * The tables are minimal but functional: they advertise the FADT reset
 * register (port 0xCF9, value 0x06) for reboot and the PM1a control
 * block (port 0x604) for S5 sleep (soft-off). The DSDT provides the
 * \_S5 sleep package so the kernel can extract SLP_TYPx values.
 *
 * Memory layout (physical):
 *   0x000F0000  RSDP (36 bytes, ACPI v2.0)
 *   0x000F0020  RSDT (36 + 4*4 = 52 bytes → padded to 0x60)
 *   0x000F0100  FADT (132 bytes → padded to 0x100)
 *   0x000F0200  MADT (44 + 30 = 74 bytes → padded to 0x100)
 *   0x000F0300  DSDT (~200 bytes)
 */

class ACPI {
    constructor(memory) {
        this.mem = memory;
    }

    /** Install all ACPI tables into physical memory. */
    installTables() {
        this.writeRSDP();
        this.writeRSDT();
        this.writeFADT();
        this.writeMADT();
        this.writeDSDT();
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    /** Compute ACPI checksum: all bytes in the buffer sum to 0 mod 256. */
    _checksum(buf) {
        let sum = 0;
        for (let i = 0; i < buf.length; i++) sum = (sum + buf[i]) & 0xFF;
        return (0x100 - sum) & 0xFF;
    }

    /** Write a byte array to physical memory. */
    _writeBytes(phys, buf) {
        for (let i = 0; i < buf.length; i++) this.mem.write8(phys + i, buf[i]);
    }

    /** Build an SDT header (36 bytes) with a given signature and data payload.
     *  Returns a Uint8Array of (36 + data.length) bytes with checksum set. */
    _makeSDT(signature, revision, data) {
        const totalLen = 36 + data.length;
        const buf = new Uint8Array(totalLen);
        const enc = (s, off, len) => { for (let i = 0; i < len; i++) buf[off + i] = s.charCodeAt(i); };

        enc(signature, 0, 4);                    // Signature
        buf[4] = totalLen & 0xFF;                // Length (dword, LE)
        buf[5] = (totalLen >> 8) & 0xFF;
        buf[6] = (totalLen >> 16) & 0xFF;
        buf[7] = (totalLen >> 24) & 0xFF;
        buf[8] = revision;                       // Revision
        buf[9] = 0;                              // Checksum placeholder
        enc('OEMXXX', 10, 6);                    // OEM ID
        enc('OEMTABL\0', 16, 8);                 // OEM Table ID
        buf[24] = 1; buf[25] = 0; buf[26] = 0; buf[27] = 0;  // OEM revision
        enc('TEST', 28, 4);                      // Creator ID
        buf[32] = 1; buf[33] = 0; buf[34] = 0; buf[35] = 0;  // Creator revision
        for (let i = 0; i < data.length; i++) buf[36 + i] = data[i];
        buf[9] = this._checksum(buf);            // Set checksum
        return buf;
    }


    // ── RSDP (Root System Description Pointer) ──────────────────────────
    // Physical 0xF0000, 20 bytes (ACPI v1.0).
    // Points to the RSDT at 0x000F0020.
    // Using v1 (20 bytes) avoids overlap with the RSDT at 0xF0020.

    writeRSDP() {
        const buf = new Uint8Array(20);
        const enc = (s, off, len) => { for (let i = 0; i < len; i++) buf[off + i] = s.charCodeAt(i); };

        enc('RSD PTR ', 0, 8);                   // Signature
        buf[8] = 0;                              // Checksum placeholder
        enc('OEMXXX', 9, 6);                     // OEM ID
        buf[15] = 0x00;                          // Revision 0 (ACPI v1.0)
        // RSDT address (32-bit, LE) at offset 16
        buf[16] = 0x20; buf[17] = 0x00; buf[18] = 0xF0; buf[19] = 0x0F;  // 0x000F0020

        let sum = 0;
        for (let i = 0; i < 20; i++) sum = (sum + buf[i]) & 0xFF;
        buf[8] = (0x100 - sum) & 0xFF;

        this._writeBytes(0x000F0000, buf);
    }


    // ── RSDT (Root System Description Table) ────────────────────────────
    // Physical 0xF0020. Lists 4 tables: FADT, MADT, DSDT (and a spare).

    writeRSDT() {
        // 4 entries: FADT, MADT, DSDT, (spare/reserved)
        const entries = [
            0x000F0100,  // FADT
            0x000F0200,  // MADT
            0x000F0300,  // DSDT
            0x00000000,  // Reserved / future HPET
        ];

        const data = new Uint8Array(entries.length * 4);
        for (let i = 0; i < entries.length; i++) {
            data[i * 4 + 0] = entries[i] & 0xFF;
            data[i * 4 + 1] = (entries[i] >> 8) & 0xFF;
            data[i * 4 + 2] = (entries[i] >> 16) & 0xFF;
            data[i * 4 + 3] = (entries[i] >> 24) & 0xFF;
        }
        const buf = this._makeSDT('RSDT', 1, data);
        this._writeBytes(0x000F0020, buf);
    }


    // ── FADT (Fixed ACPI Description Table) ─────────────────────────────
    // Physical 0xF0100. 132 bytes (SDT header + 96 bytes of FADT fields).
    // Provides:
    //   - PM1a_CNT_BLK at port 0x604 (for S5 shutdown)
    //   - RESET_REG at I/O port 0xCF9, reset value 0x06
    //   - DSDT pointer to 0xF0300
    //   - SCI_INT = 9, IAPC_BOOT_ARCH = 3 (VGA + PS/2)

    writeFADT() {
        const data = new Uint8Array(96);  // 132 - 36 = 96 bytes after SDT header

        // Offset 36: FACS (dword) = 0
        // Offset 40: DSDT (dword) = 0x000F0300
        data[4] = 0x00; data[5] = 0x03; data[6] = 0x0F; data[7] = 0x00;

        // Offset 48: SMI_CMD (byte) = 0 (no SMM)
        data[12] = 0;
        // Offset 49: ACPI_ENABLE = 0
        data[13] = 0;
        // Offset 50: ACPI_DISABLE = 0
        data[14] = 0;
        // Offset 51: S4BIOS_REQ = 0
        data[15] = 0;
        // Offset 52: PSTATE_CNT = 0
        data[16] = 0;

        // Offset 56: PM1a_EVT_BLK (dword) = 0x600
        data[20] = 0x00; data[21] = 0x06;
        // Offset 60: PM1b_EVT_BLK (dword) = 0
        // Offset 64: PM1a_CNT_BLK (dword) = 0x604
        data[28] = 0x04; data[29] = 0x06;
        // Offset 68: PM1b_CNT_BLK (dword) = 0
        // Offset 72: PM2_CNT_BLK (dword) = 0
        // Offset 76: PM_TMR_BLK (dword) = 0x608
        data[40] = 0x08; data[41] = 0x06;
        // Offset 80: GPE0_BLK = 0
        // Offset 84: GPE1_BLK = 0

        // Offset 88: PM1_EVT_LEN (byte) = 4
        data[52] = 4;
        // Offset 89: PM1_CNT_LEN (byte) = 2
        data[53] = 2;
        // Offset 90: PM2_CNT_LEN = 0
        data[54] = 0;
        // Offset 91: PM_TMR_LEN = 4
        data[55] = 4;
        // Offset 92: GPE0_BLK_LEN = 0
        // Offset 93: GPE1_BLK_LEN = 0
        // Offset 94: GPE1_BASE = 0
        // Offset 95: CST_CNT = 0

        // Offset 96: C2_LATENCY (word) = 0
        // Offset 98: C3_LATENCY (word) = 0
        // Offset 100: FLUSH_SIZE (word) = 0
        // Offset 102: FLUSH_STRIDE (word) = 0
        // Offset 104: DUTY_OFFSET = 0
        // Offset 105: DUTY_WIDTH = 0
        // Offset 106: DAY_ALARM = 0
        // Offset 107: MONTH_ALARM = 0
        // Offset 108: CENTURY = 0

        // Offset 109: IAPC_BOOT_ARCH (word) = 0x03 (VGA + PS/2 present)
        data[73] = 0x03;
        // Offset 111: reserved

        // Offset 112: FLAGS (dword) = bit 0 (WBINVD) | bit 2 (SLP_BUTTON)
        data[76] = 0x05;

        // Offset 116: RESET_REG (12-byte Generic Address Structure)
        data[80] = 1;    // AddressSpaceId: 1 = system I/O
        data[81] = 8;    // RegisterBitWidth: 8
        data[82] = 0;    // RegisterBitOffset: 0
        data[83] = 0;    // Reserved
        data[84] = 0xF9; // Address (qword, LE): 0xCF9
        data[85] = 0x0C;
        data[86] = 0;
        data[87] = 0;
        data[88] = 0;
        data[89] = 0;
        data[90] = 0;
        data[91] = 0;
        data[92] = 1;    // AccessSize: 1 = byte access

        // Offset 129: RESET_VALUE (byte) = 0x06
        data[93] = 0x06;

        // Offset 130-131: reserved

        const buf = this._makeSDT('FACP', 2, data);
        this._writeBytes(0x000F0100, buf);
    }


    // ── MADT (Multiple APIC Description Table) ──────────────────────────
    // Physical 0xF0200. Contains:
    //   1. Processor Local APIC (8 bytes) — CPU 0, APIC ID 0, enabled
    //   2. I/O APIC (12 bytes) — base 0xFEC00000, GSI base 0
    //   3. 8259A Source Override (10 bytes) — IRQ0 → GSI2
    //
    // Total MADT: 44 (header) + 8 + 12 + 10 = 74 bytes

    writeMADT() {
        // Local APIC address: not emulated yet, but we report 0xFEE00000
        // which is the standard x86 local APIC base.
        const lapicAddr = 0xFEE00000;

        const entries = [];

        // 1. Processor Local APIC (type=0, length=8)
        entries.push(0, 8, 0, 0, 1, 0, 0, 0);  // type, len, procID, apicID, flags

        // 2. I/O APIC (type=1, length=12)
        //    ioapic_addr = 0xFEC00000, gsi_base = 0
        entries.push(1, 12,                                // type, length
                     0,                                     // I/O APIC ID
                     0,                                     // reserved
                     0x00, 0x00, 0xC0, 0xFE,               // address = 0xFEC00000 (LE)
                     0x00, 0x00, 0x00, 0x00);              // GSI base = 0

        // 3. 8259A Source Override (type=2, length=10)
        //    Maps ISA IRQ0 → GSI 2 (standard for APIC mode)
        entries.push(2, 10,                                // type, length
                     0,                                     // bus = ISA (0)
                     0,                                     // IRQ source = 0
                     2, 0,                                  // GSI = 2
                     0x00, 0x00, 0x00, 0x00);              // flags = 0 (same as PIC)

        const data = new Uint8Array(entries);

        // Prepend the local APIC address (4 bytes) at the start of MADT data
        // In the MADT, the local interrupt controller address is at SDT offset 36
        // (right after the 36-byte header). We embed it via the data array.
        const madtData = new Uint8Array(4 + entries.length);
        madtData[0] = lapicAddr & 0xFF;
        madtData[1] = (lapicAddr >> 8) & 0xFF;
        madtData[2] = (lapicAddr >> 16) & 0xFF;
        madtData[3] = (lapicAddr >> 24) & 0xFF;
        for (let i = 0; i < entries.length; i++) madtData[4 + i] = entries[i];

        const buf = this._makeSDT('APIC', 1, madtData);
        this._writeBytes(0x000F0200, buf);
    }


    // ── DSDT (Differentiated System Description Table) ──────────────────
    // Physical 0xF0300. Minimal AML bytecode.
    //
    // Defines:
    //   Scope(\_SB) {
    //       Name(\_S5, Package(4) { 0x05, 0x05, 0x00, 0x00 })
    //   }
    //
    // This lets acpi_shutdown read the SLP_TYPx values.

    writeDSDT() {
        // AML: Scope(\_SB_) { Name(\_S5, Package(4){0x05,0x05,0x00,0x00}) }
        //
        // 0x10 = Scope opcode
        // 0x40 + length = PkgLength
        // 0x5F, 0x5F = "_" (NameString prefix for \)
        // 0x04 = NameSeg length? Actually:
        //   \_SB_ = 0x5F, 0x53, 0x42, 0x5F
        // 
        // Bytecode breakdown:
        //   0x10 0x20      Scope(PkgLength=0x20) —— Scope(\_SB_)
        //     0x5F 0x53 0x42 0x5F   NameString = "_SB_"
        //     0x08 0x5F 0x53 0x35 0x5F   Name(\_S5)
        //       0x12 0x0C 0x04      Package(4 elements, PkgLength=0x0C)
        //         0x0A 0x05         ByteConst(0x05) — SLP_TYPa
        //         0x0A 0x05         ByteConst(0x05) — SLP_TYPb
        //         0x00               Zero
        //         0x00               Zero

        const aml = [
            0x10, 0x20,                               // Scope(PkgLength=32)
            0x5F, 0x53, 0x42, 0x5F,                  //   \_SB_
            0x08,                                      //   NameOp
            0x5F, 0x53, 0x35, 0x5F,                  //     \_S5
            0x12, 0x0C,                               //     Package(PkgLength=12)
            0x04,                                      //       4 elements
            0x0A, 0x05,                               //       Byte(0x05) — SLP_TYPa
            0x0A, 0x05,                               //       Byte(0x05) — SLP_TYPb
            0x00,                                      //       Zero
            0x00,                                      //       Zero
        ];

        // Ensure proper definition block header for DSDT.
        // DSDT is a Definition Block. It starts with the SDT header, then AML.
        // The AML body is the entire Definition Block.
        const buf = this._makeSDT('DSDT', 1, new Uint8Array(aml));
        this._writeBytes(0x000F0300, buf);
    }
}


// Node.js / browser export
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ACPI };
}
