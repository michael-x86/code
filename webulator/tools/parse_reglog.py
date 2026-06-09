#!/usr/bin/env python3
"""Parse webulator reglog files. Filter to halted=false, show ASCII register contents."""

import re
import sys
import os

REGLINE_RE = re.compile(
    r'T=(?P<tstates>\d+)\s+'
    r'EIP=(?P<eip>[0-9A-Fa-f]{8})\s+'
    r'EAX=(?P<eax>[0-9A-Fa-f]{8})\s+'
    r'EBX=(?P<ebx>[0-9A-Fa-f]{8})\s+'
    r'ECX=(?P<ecx>[0-9A-Fa-f]{8})\s+'
    r'EDX=(?P<edx>[0-9A-Fa-f]{8})\s+'
    r'ESI=(?P<esi>[0-9A-Fa-f]{8})\s+'
    r'EDI=(?P<edi>[0-9A-Fa-f]{8})\s+'
    r'EBP=(?P<ebp>[0-9A-Fa-f]{8})\s+'
    r'ESP=(?P<esp>[0-9A-Fa-f]{8})\s+'
    r'EFL=(?P<efl>[0-9A-Fa-f]{8})\s+'
    r'CS=(?P<cs>[0-9A-Fa-f]{4})\s+'
    r'DS=(?P<ds>[0-9A-Fa-f]{4})\s+'
    r'SS=(?P<ss>[0-9A-Fa-f]{4})\s+'
    r'CR0=(?P<cr0>[0-9A-Fa-f]{8})\s+'
    r'CR3=(?P<cr3>[0-9A-Fa-f]{8})\s+'
    r'halted=(?P<halted>true|false)'
)


def hex_to_ascii_le(hex_str):
    """Convert hex string to ASCII in little-endian (x86 memory) order."""
    try:
        raw = bytes.fromhex(hex_str)
    except ValueError:
        return '<bad hex>'
    chars = []
    for b in reversed(raw):  # LSB first = lowest memory address
        chars.append(chr(b) if 0x21 <= b <= 0x7E else '.')
    return ''.join(chars)


def has_printable(hex_str):
    """Check if hex value contains any printable non-space bytes."""
    try:
        raw = bytes.fromhex(hex_str)
    except ValueError:
        return False
    return any(0x21 <= b <= 0x7E for b in raw)


COLOR_RED = '\033[31m'
COLOR_GREEN = '\033[32m'
COLOR_YELLOW = '\033[33m'
COLOR_CYAN = '\033[36m'
COLOR_BOLD = '\033[1m'
COLOR_RESET = '\033[0m'

REG_32 = ['EIP', 'EAX', 'EBX', 'ECX', 'EDX', 'ESI', 'EDI', 'EBP', 'ESP']
REG_16 = ['CS', 'DS', 'SS']


def format_ascii_column(hex_val, width=8):
    """Format a 32-bit register value with little-endian ASCII interpretation."""
    ascii_le = hex_to_ascii_le(hex_val)
    return f'{ascii_le}'


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='Parse webulator reglog files. Filter to halted=false lines '
                    'and show register ASCII contents.'
    )
    parser.add_argument('file', nargs='?', help='Path to reglog file')
    parser.add_argument('-a', '--all', action='store_true',
                        help='Show all lines including halted=true')
    parser.add_argument('-n', '--limit', type=int, default=0,
                        help='Show only first N matching lines')
    parser.add_argument('-t', '--tstates', type=int, default=0,
                        help='Show only lines after this T-states value')
    parser.add_argument('-r', '--reg', action='append', default=[],
                        help='Show only specified registers (e.g. EAX, EIP). Repeatable.')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show all registers, not just those with printable ASCII')
    parser.add_argument('-c', '--compact', action='store_true',
                        help='Compact output: one line per log entry, ASCII summary only')
    args = parser.parse_args()

    filepath = args.file

    # If no file given, try to find the latest reglog in the project
    if not filepath:
        candidates = sorted(
            [f for f in os.listdir('.') if f.startswith('reglog-') and f.endswith('.txt')],
            reverse=True
        )
        if candidates:
            filepath = candidates[0]
            print(f'{COLOR_CYAN}Using: {filepath}{COLOR_RESET}', file=sys.stderr)
        else:
            print('No reglog file specified and none found in current directory.',
                  file=sys.stderr)
            sys.exit(1)

    if not os.path.exists(filepath):
        print(f'File not found: {filepath}', file=sys.stderr)
        sys.exit(1)

    lines = []
    with open(filepath, 'r') as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue
            m = REGLINE_RE.match(line)
            if not m:
                continue
            entry = m.groupdict()
            entry['halted'] = entry['halted'] == 'true'
            entry['tstates'] = int(entry['tstates'])
            lines.append(entry)

    # Filter
    if not args.all:
        lines = [l for l in lines if not l['halted']]
    if args.tstates:
        lines = [l for l in lines if l['tstates'] >= args.tstates]
    if args.limit:
        lines = lines[:args.limit]

    if not lines:
        print('No matching lines found.', file=sys.stderr)
        sys.exit(0)

    # Determine which registers to show
    if args.reg:
        show_regs = [r.upper() for r in args.reg]
    else:
        show_regs = REG_32 + REG_16

    if args.compact:
        # Compact mode: one line per entry, EIP + requested registers with ASCII
        compact_regs = [r for r in show_regs if r in REG_32]
        for entry in lines:
            parts = [f'T={entry["tstates"]}']
            parts.append(f'EIP={entry["eip"]}')
            ascii_parts = []
            for reg in compact_regs:
                val = entry[reg.lower()]
                ascii_le = hex_to_ascii_le(val)
                if has_printable(val) or args.verbose:
                    ascii_parts.append(f'{reg}={val}({ascii_le})')
            if ascii_parts:
                parts.append(' '.join(ascii_parts))
            elif args.verbose:
                parts.append('(no ASCII)')
            print(' | '.join(parts))
    else:
        # Verbose mode: block per entry
        for i, entry in enumerate(lines):
            if i > 0:
                print()
            print(f'{COLOR_BOLD}{COLOR_CYAN}--- T={entry["tstates"]} '
                  f'EIP={entry["eip"]} halted={entry["halted"]} ---{COLOR_RESET}')

            printed_any = False
            for reg in show_regs:
                if reg in REG_32:
                    val = entry[reg.lower()]
                    ascii_le = hex_to_ascii_le(val)
                    if args.verbose or has_printable(val):
                        marker = f' {COLOR_GREEN}→ LE:{COLOR_RESET} {COLOR_YELLOW}"{ascii_le}"{COLOR_RESET}'
                        print(f'  {reg:<5} {val} {marker}')
                        printed_any = True
                elif reg in REG_16:
                    val = entry[reg.lower()]
                    print(f'  {reg:<5} {val}')
                elif reg == 'EFL':
                    print(f'  EFL    {entry["efl"]}')
                elif reg == 'CR0':
                    print(f'  CR0    {entry["cr0"]}')
                elif reg == 'CR3':
                    print(f'  CR3    {entry["cr3"]}')

            if not printed_any and not args.verbose:
                print(f'  {COLOR_RED}(no ASCII content in registers){COLOR_RESET}')

    print(f'\n{COLOR_CYAN}{len(lines)} matching lines{COLOR_RESET}', file=sys.stderr)


if __name__ == '__main__':
    main()
