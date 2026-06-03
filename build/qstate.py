#!/usr/bin/env python3
import socket, subprocess, sys, time, os, re
SOCK="/tmp/qmon6.sock"
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
time.sleep(1.2)
def word(addr):
    out=mon(f"x /1wx 0x{addr:08x}",0.25)
    m=re.search(r":\s+0x([0-9a-fA-F]+)",out)
    return m.group(1) if m else "?"
print("=== registers ===")
print(mon("info registers",0.6))
print("exec_vbase   =",word(0xc0103396))
print("exec_pages   =",word(0xc010339a))
print("=== PDE for 0xC0800000 (idx770) in page_directory @0xC0111000+0xC08 ===")
print(mon("xp /1wx 0x111c08",0.4))
print(mon("x /1wx 0xc0111c08",0.4))
print("=== heap_page_table_0 @0xC0114000 PTE[0] ===")
print(mon("x /4wx 0xc0114000",0.4))
ev=word(0xc0103396)
try:
    eva=int(ev,16)
    if 0xc0000000<eva<0xff000000:
        print(f"=== heap @exec_vbase 0x{eva:08x} (binary code?) ===")
        print(mon(f"x /8wx 0x{eva:08x}",0.4))
except: pass
mon("quit",0.1); q.terminate()
try: q.wait(timeout=3)
except Exception: q.kill()
