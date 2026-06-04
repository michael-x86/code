#!/usr/bin/env python3
"""Drive the OS through QEMU monitor, type the full enigma session, dump screen."""
import socket, subprocess, sys, time, os

SOCK = "/tmp/qshot.sock"
BOOT = os.path.join(os.path.dirname(__file__), "boot.img")
FS   = os.path.join(os.path.dirname(__file__), "fs.img")
OUT  = os.path.join(os.path.dirname(__file__), "shot.ppm")

if os.path.exists(SOCK): os.remove(SOCK)
q = subprocess.Popen(
    ["qemu-system-i386",
     "-drive", f"format=raw,file={BOOT},index=0,if=ide",
     "-drive", f"format=raw,file={FS},index=1,if=ide",
     "-m", "128", "-display", "none",
     "-monitor", f"unix:{SOCK},server,nowait"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.1)
time.sleep(0.5)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK)
time.sleep(0.3)

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
    s.sendall((l + "\n").encode())
    time.sleep(w)
    return rc(w)

# wait for shell to be ready (sleep is from qshot.py)
rc(); time.sleep(4.0)

# Type "enigma" + Enter
for ch in "enigma":
    mon(f"sendkey {ch}", 0.08)
mon("sendkey ret", 0.4)
time.sleep(0.5)

# Type the date "20240101" + Enter
for ch in "20240101":
    mon(f"sendkey {ch}", 0.08)
mon("sendkey ret", 0.4)
time.sleep(0.5)

# Type "AAAA" (will be encrypted character-by-character)
for ch in "AAAA":
    mon(f"sendkey shift-{ch.lower()}", 0.08)  # uppercase via shift
time.sleep(0.3)
# A newline: empty line exits
mon("sendkey ret", 0.4)
time.sleep(0.5)

# Dump screen
if os.path.exists(OUT): os.remove(OUT)
print(mon(f"screendump {OUT}", 0.6))
time.sleep(0.3)
mon("quit", 0.1)
q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
print("wrote", OUT, os.path.getsize(OUT) if os.path.exists(OUT) else "MISSING")
