#!/usr/bin/env python3
"""Extract virtual addresses of known symbols from a kernel binary.

Scans the flat binary for the specific instruction patterns emitted when the
kernel references the requested symbols, and recovers the virtual address
from the instruction encoding.

Usage: extract_syms.py <kernel.bin> <symbol1,symbol2,...>

Supported symbols:
  acpi_init       — first instruction of acpi_init: 56 (push esi)
  rsdt_found      — mov byte [rsdt_found], 1        = C6 05 xx xx xx xx 01
  fadt_address    — mov [fadt_address], eax          = A3 xx xx xx xx
"""
import json
import sys

VIRT_BASE = 0xC0100000

def find_acpi_init(data):
    """acpi_init starts with push esi; push edi; push eax; push ecx; push edx."""
    needle = bytes([0x56, 0x57, 0x50, 0x51, 0x52])
    idx = data.find(needle)
    if idx >= 0:
        return VIRT_BASE + idx
    return None

def find_rsdt_found(data):
    """mov byte [rsdt_found], 1  =>  C6 05 <addr:4> 01
    The target address must be in the kernel data section (0xC0104000+)
    to avoid false positives from user-space code embedded in the binary."""
    needle = bytes([0xC6, 0x05])
    idx = 0
    while True:
        idx = data.find(needle, idx)
        if idx < 0:
            break
        if idx + 6 < len(data) and data[idx + 6] == 0x01:
            addr = int.from_bytes(data[idx+2:idx+6], 'little')
            if 0xC0104000 <= addr <= 0xC0110000:
                return addr
        idx += 1
    return None

def find_fadt_address(data):
    """mov [fadt_address], eax  =>  A3 <addr:4>
    We locate the rsdt_found reference (C6 05 <addr> 01), then find
    the A3 instruction at file offset + 47 bytes (which is where the
    listing shows mov [fadt_address], eax)."""
    target = bytes([0xC6, 0x05])
    idx = 0
    while True:
        idx = data.find(target, idx)
        if idx < 0:
            break
        if idx + 6 < len(data) and data[idx + 6] == 0x01:
            addr = int.from_bytes(data[idx+2:idx+6], 'little')
            if 0xC0104000 <= addr <= 0xC0110000:
                # rsdt_found reference found. fadt_address reference is
                # 47 bytes later in the source (listing offsets: 0x0929->0x0958)
                fadt_idx = idx + 0x2F  # 47 bytes
                if fadt_idx + 5 < len(data) and data[fadt_idx] == 0xA3:
                    fadt_addr = int.from_bytes(data[fadt_idx+1:fadt_idx+5], 'little')
                    if 0xC0104000 <= fadt_addr <= 0xC0110000:
                        return fadt_addr
        idx += 1
    return None

def main():
    bin_path = sys.argv[1]
    wanted = set(sys.argv[2].split(","))

    with open(bin_path, 'rb') as f:
        data = f.read()

    result = {}

    for sym in wanted:
        addr = None
        if sym == "acpi_init":
            addr = find_acpi_init(data)
        elif sym == "rsdt_found":
            addr = find_rsdt_found(data)
        elif sym == "fadt_address":
            addr = find_fadt_address(data)
        if addr is not None:
            result[sym] = f"0x{addr:08X}"
        else:
            result[sym] = None

    print(json.dumps(result))

if __name__ == "__main__":
    main()
