#!/usr/bin/env python3
import socket, subprocess, sys, time, os, re
SOCK="/tmp/qmon5.sock"
BOOT=os.path.join(os.path.dirname(__file__),"boot.img")
FS=os.path.join(os.path.dirname(__file__),"fs.img")
cmd=sys.argv[1] if len(sys.argv)>1 else "pwd"
if os.path.exists(SOCK): os.remove(SOCK)
q=subprocess.Popen(["qemu-system-i386","-drive",f"format=raw,file={BOOT},index=0,if=ide",
 "-drive",f"format=raw,file={FS},index=1,if=ide","-m","128","-display","none",
 "-monitor",f"unix:{SOCK},server,nowait"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.1)
time.sleep(0.5)
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(SOCK); time.sleep(0.3)
def rc(t=0.4):
    s.settimeout(t); d=b""
    try:
        while True:
            c=s.recv(4096)
            if not c: break
            d+=c
    except socket.timeout: pass
    return d.decode(errors="replace")
def mon(l,w=0.3):
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
mon("sendkey ret",0.4)
time.sleep(1.0)
# Dump VGA text framebuffer at physical 0xb8000, 80x25
out=mon("xp /2000hx 0xb8000",1.0)
# parse hex words; low byte = char
vals=re.findall(r"0x([0-9a-fA-F]{4})",out)
chars=[]
for v in vals:
    c=int(v,16)&0xff
    chars.append(chr(c) if 32<=c<127 else ' ')
scr="".join(chars)
print("=== VGA SCREEN ===")
for row in range(25):
    line=scr[row*80:(row+1)*80].rstrip()
    if line: print(f"{row:2}|{line}")
mon("quit",0.1); q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
