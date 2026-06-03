#!/usr/bin/env python3
"""Drive the OS in QEMU headless: boot, type a command, dump VGA text."""
import socket, subprocess, sys, time, os

SOCK = "/tmp/qmon.sock"
BOOT = os.path.join(os.path.dirname(__file__), "boot.img")
FS = os.path.join(os.path.dirname(__file__), "fs.img")

cmd = sys.argv[1] if len(sys.argv) > 1 else "pwd"
boot_wait = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0

if os.path.exists(SOCK):
    os.remove(SOCK)

qemu = subprocess.Popen([
    "qemu-system-i386",
    "-drive", f"format=raw,file={BOOT},index=0,if=ide",
    "-drive", f"format=raw,file={FS},index=1,if=ide",
    "-m", "128",
    "-display", "none",
    "-monitor", f"unix:{SOCK},server,nowait",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# wait for socket
for _ in range(100):
    if os.path.exists(SOCK):
        break
    time.sleep(0.1)
time.sleep(0.5)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK)
time.sleep(0.3)

def recv_all(timeout=0.5):
    s.settimeout(timeout)
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data.decode(errors="replace")

def mon(line, wait=0.3):
    s.sendall((line + "\n").encode())
    time.sleep(wait)
    return recv_all()

recv_all()  # banner
print(f"[boot wait {boot_wait}s]")
time.sleep(boot_wait)

# Map characters to qemu sendkey names
keymap = {
    ' ': 'spc', '-': 'minus', '/': 'slash', '.': 'dot',
}
def keyname(ch):
    if ch in keymap:
        return keymap[ch]
    if ch.isdigit():
        return ch
    if ch.isalpha():
        return ch.lower()
    return None

for ch in cmd:
    kn = keyname(ch)
    if kn is None:
        print(f"  (skip unmappable {ch!r})")
        continue
    mon(f"sendkey {kn}", 0.08)
mon("sendkey ret", 0.6)
time.sleep(1.0)

print("==== REGISTERS ====")
print(mon("info registers", 0.5))
print("==== EIP SAMPLES ====")
import re
for i in range(5):
    r = mon("info registers", 0.25)
    m = re.search(r"EIP=([0-9a-fA-F]+)", r)
    print(f"  sample {i}: EIP={m.group(1) if m else '?'}")
print("==== DISASM around eip ====")
print(mon("x /24i 0xc0100670", 0.5))
print("==== TASK STATE (cursor/tick alive?) ====")
# Dump VGA text buffer at physical 0xb8000 (80x25, 2 bytes/cell)
out = mon("xp /4000xb 0xb8000", 0.8)

# Parse the xp output into bytes
vals = []
for tok in out.replace(":", " ").split():
    if tok.startswith("0x") and len(tok) <= 4:
        try:
            vals.append(int(tok, 16))
        except ValueError:
            pass

# Reconstruct the 80x25 text screen (take even indices = chars)
chars = vals[0::2]
rows = []
for r in range(25):
    row = chars[r*80:(r+1)*80]
    line = "".join(chr(c) if 32 <= c < 127 else (" " if c == 0 else ".") for c in row)
    rows.append(line.rstrip())
print("==== SCREEN ====")
print("\n".join(rows))
print("==== END ====")

mon("quit", 0.2)
qemu.terminate()
try:
    qemu.wait(timeout=3)
except Exception:
    qemu.kill()
