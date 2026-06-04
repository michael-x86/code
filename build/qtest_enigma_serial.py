#!/usr/bin/env python3
# Boot the OS headless with serial stdio, type the enigma command, capture output
import subprocess, sys, time, os, threading, select
BD=os.path.dirname(os.path.abspath(__file__))
BOOT=os.path.join(BD,"boot.img")
FS=os.path.join(BD,"fs.img")

CMD = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else "enigma e 20020202 hello"
DELAY = 4.0

q = subprocess.Popen([
    "qemu-system-i386",
    "-drive", f"format=raw,file={BOOT},index=0,if=ide",
    "-drive", f"format=raw,file={FS},index=1,if=ide",
    "-m", "128",
    "-display", "none",
    "-serial", "stdio",
    "-monitor", "none",
], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)

def read_qemu(timeout=4.0):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r,_,_ = select.select([q.stdout], [], [], 0.1)
        if r:
            data = q.stdout.read(4096)
            if not data: break
            out += data
    return out

# Wait for boot prompt
print("Waiting for boot...", flush=True)
out1 = read_qemu(4.0)
print("=== After boot ===")
sys.stdout.write(out1.decode(errors="replace"))
sys.stdout.flush()

# Type the command
KM = {' ': ' ', '-': '-', '/': '/', '.': '.'}
def to_qemu(c):
    if c == '\n': return 'ret'
    if c in KM: return c
    if c.isdigit() or c.isalpha(): return c
    return None

for ch in CMD:
    k = to_qemu(ch)
    if k:
        q.stdin.write(f"sendkey {k}\n".encode())
        q.stdin.flush()
        time.sleep(0.05)
q.stdin.write(b"sendkey ret\n")
q.stdin.flush()

# Wait for output
print("\n=== After command ===", flush=True)
out2 = read_qemu(DELAY)
sys.stdout.write(out2.decode(errors="replace"))
sys.stdout.flush()

q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
