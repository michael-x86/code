#!/usr/bin/env python3
# One-off: boot, run elite, press a sequence of keys, screenshot.
import socket, subprocess, sys, time, os
SOCK="/tmp/qshot2.sock"
BD=os.path.dirname(os.path.abspath(__file__))
BOOT=os.path.join(BD,"boot.img"); FS=os.path.join(BD,"fs.img")
OUT=os.path.join(BD,"shot.ppm")
keys=sys.argv[1] if len(sys.argv)>1 else "i"   # keys to press after launch
delay=float(sys.argv[2]) if len(sys.argv)>2 else 2.0
if os.path.exists(SOCK): os.remove(SOCK)
q=subprocess.Popen(["qemu-system-i386","-drive",f"format=raw,file={BOOT},index=0,if=ide",
 "-drive",f"format=raw,file={FS},index=1,if=ide","-m","128","-display","none",
 "-monitor",f"unix:{SOCK},server,nowait"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.1)
time.sleep(0.5)
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(SOCK); time.sleep(0.3)
def rc(t=0.3):
    s.settimeout(t); d=b""
    try:
        while True:
            c=s.recv(4096)
            if not c: break
            d+=c
    except socket.timeout: pass
    return d.decode(errors="replace")
def mon(l,w=0.2):
    s.sendall((l+"\n").encode()); time.sleep(w); return rc()
rc(); time.sleep(4.0)
for ch in "elite":
    mon(f"sendkey {ch}",0.07)
mon("sendkey ret",0.5)
time.sleep(1.5)
for ch in keys:
    mon(f"sendkey {ch}",0.2)
time.sleep(delay)
if os.path.exists(OUT): os.remove(OUT)
mon(f"screendump {OUT}",0.6)
time.sleep(0.4)
mon("quit",0.1); q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
print("wrote", OUT, os.path.getsize(OUT) if os.path.exists(OUT) else "MISSING")
