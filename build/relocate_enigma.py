#!/usr/bin/env python3
"""Convert `default rel`-style memory accesses in enigma.asm to explicit
`ebp + label` form. This is needed because NASM 32-bit `default rel` doesn't
actually produce RIP-relative encodings; the OS's userland ABI uses ebp as
the base pointer.

Heuristic: any `[label]` or `[label + ...]` (where label is a known local
symbol) becomes `[ebp + label + ...]`. Register-indirect forms like `[eax]`,
`[ebx+ecx]`, `[edi]` are left alone.
"""
import re, sys, subprocess, os

ASM = sys.argv[1] if len(sys.argv) > 1 else "commands/enigma.asm"

# Find all locally-defined labels (not `extern`, not starting with `.`)
defined = set()
with open(ASM) as f:
    for line in f:
        # label:  (label followed by colon, possibly with a local prefix)
        m = re.match(r"\s*([A-Za-z_][\w\.\$]*):", line)
        if m:
            lab = m.group(1)
            if not lab.startswith('.'):
                defined.add(lab)

# Print a sample
print(f"Found {len(defined)} labels", file=sys.stderr)

# Read source
with open(ASM) as f:
    src = f.read()

# Sort labels by length (longest first) so substrings don't get partial matches
labs = sorted(defined, key=lambda s: -len(s))

# Replace `[label` with `[ebp + label` only when not preceded by al/ah/etc.
# We need to be careful: in `byte [eax]`, the `[eax]` shouldn't change.
# Strategy: find all `[...]` blocks, then within each block, if a label
# appears, prefix with `ebp + `.

def fix_mem(m):
    inner = m.group(1)
    # Walk through inner, find label tokens at start or after +/-
    # Match a label that is preceded by `[`, `+`, `-`, or `,` (or whitespace)
    # and followed by `+`, `-`, `]`, or `*` (register scaling)
    out = []
    i = 0
    while i < len(inner):
        # Try to match a label at current position
        matched = None
        for lab in labs:
            if inner[i:].startswith(lab):
                # Check what precedes
                # Look at out[-1] (or whitespace)
                prev_idx = len(out) - 1
                # Skip spaces in out
                while prev_idx >= 0 and out[prev_idx] == ' ':
                    prev_idx -= 1
                prev = out[prev_idx] if prev_idx >= 0 else ''
                # If preceded by al/ah/etc or part of a register, skip
                if prev and prev[-1].isalnum() and prev[-1] not in '[,+-*':
                    continue
                matched = lab
                break
        if matched:
            # Check what follows
            nxt = inner[i + len(matched):i + len(matched) + 1]
            if nxt and nxt[0].isalnum() and nxt[0] not in '_':
                # Not a real label match (e.g., label is prefix of something)
                out.append(inner[i])
                i += 1
                continue
            # Insert ebp + if not already preceded by ebp
            prev_idx = len(out) - 1
            while prev_idx >= 0 and out[prev_idx] == ' ':
                prev_idx -= 1
            prev = out[prev_idx] if prev_idx >= 0 else ''
            if prev != 'ebp':
                # Determine operator to insert
                if prev in ('', '[', ',', '+', '-'):
                    out.append('ebp')
                    if prev in ('', '['):
                        out.append(' + ')
                    else:
                        out.append(' ')
                else:
                    out.append('ebp + ')
            out.append(matched)
            i += len(matched)
        else:
            out.append(inner[i])
            i += 1
    return '[' + ''.join(out) + ']'

# Match [...] memory references
pattern = re.compile(r'\[([^\[\]]*)\]')
# Note: this doesn't handle nested brackets, but enigma code doesn't use them.

new_src = pattern.sub(fix_mem, src)

# Also fix `lea esi, [label]` style: the label is at the start. Already handled.

# Write
out_path = ASM + ".ebp"
with open(out_path, 'w') as f:
    f.write(new_src)
print(f"Wrote {out_path}", file=sys.stderr)
