#!/usr/bin/env python3
# Boot the OS headless, type the enigma command + a date + "AAAA" + Enter,
# and screenshot the result.
import socket, subprocess, sys, time, os
SOCK="/tmp/qenigma.sock"
BD=os.path.dirname(os.path.abspath(__file__))
BOOT=os.path.join(BD,"boot.img")
FS=os.path.join(BD,"fs.img")
OUT=os.path.join(BD,"shot.ppm")

DATE = sys.argv[1] if len(sys.argv) > 1 else "20240101"
TEXT = sys.argv[2] if len(sys.argv) > 2 else "AAAA"
DELAY = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5

if os.path.exists(SOCK): os.remove(SOCK)
q = subprocess.Popen([
    "qemu-system-i386",
    "-drive", f"format=raw,file={BOOT},index=0,if=ide",
    "-drive", f"format=raw,file={FS},index=1,if=ide",
    "-m", "128",
    "-display", "none",
    "-monitor", f"unix:{SOCK},server,nowait",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.1)
time.sleep(0.5)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(SOCK); time.sleep(0.3)

def rc(t=0.3):
    s.settimeout(t); d = b""
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            d += c
    except socket.timeout: pass
    return d.decode(errors="replace")

def mon(l, w=0.15):
    s.sendall((l + "\n").encode()); time.sleep(w); return rc()

KM = {' ': 'spc', '-': 'minus', '/': 'slash', '.': 'dot', '_': 'shift-minus'}
def kn(c):
    if c in KM: return KM[c]
    if c.isdigit(): return c
    if c.isalpha(): return c.lower()
    return None

rc()
# Wait for boot
time.sleep(4.0)

# Type the command
for ch in "enigma":
    k = kn(ch)
    if k: mon(f"sendkey {k}", 0.05)
mon("sendkey ret", 0.4)

# Type the date
for ch in DATE:
    k = kn(ch)
    if k: mon(f"sendkey {k}", 0.05)
mon("sendkey ret", 0.6)

# Type the test plaintext
for ch in TEXT:
    k = kn(ch)
    if k: mon(f"sendkey {k}", 0.05)
mon("sendkey ret", 0.4)

# Wait then screenshot
time.sleep(DELAY)
if os.path.exists(OUT): os.remove(OUT)
mon(f"screendump {OUT}", 0.6)
time.sleep(0.4)
mon("quit", 0.1); q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
print("wrote", OUT, os.path.getsize(OUT) if os.path.exists(OUT) else "MISSING")
