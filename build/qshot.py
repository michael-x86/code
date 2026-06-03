#!/usr/bin/env python3
import socket, subprocess, sys, time, os
SOCK="/tmp/qshot.sock"
BOOT=os.path.join(os.path.dirname(__file__),"boot.img")
FS=os.path.join(os.path.dirname(__file__),"fs.img")
OUT=os.path.join(os.path.dirname(__file__),"shot.ppm")
cmd=sys.argv[1] if len(sys.argv)>1 else "gdemo"
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
km={' ':'spc','-':'minus','/':'slash','.':'dot'}
def kn(c):
    if c in km: return km[c]
    if c.isdigit(): return c
    if c.isalpha(): return c.lower()
    return None
for ch in cmd:
    k=kn(ch)
    if k: mon(f"sendkey {k}",0.07)
mon("sendkey ret",0.3)
time.sleep(delay)
if os.path.exists(OUT): os.remove(OUT)
print(mon(f"screendump {OUT}",0.6))
time.sleep(0.5)
mon("quit",0.1); q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
print("wrote", OUT, os.path.getsize(OUT) if os.path.exists(OUT) else "MISSING")
