import socket,time,sys,os
cmd=sys.argv[1] if len(sys.argv)>1 else "echo hi"
hosts=[os.environ["H805D"]] if os.environ.get("H805D") else ["192.168.10.1","192.168.1.100"]
s=None
for h in hosts:
    try:
        s=socket.socket(); s.settimeout(6); s.connect((h,2323)); break
    except Exception: s=None
if s is None:
    sys.exit("sem conexao com a multimidia (tentei: %s)"%", ".join(hosts))
time.sleep(0.4)
s.sendall(cmd.encode()+b"\necho '===END==='\n"); time.sleep(1.2)
data=b""
try:
    while True:
        d=s.recv(8192)
        if not d: break
        data+=d
        if b"===END===" in data: break
except: pass
out=data.decode(errors="replace").split("===END===")[0]
try:
    sys.stdout.buffer.write(out.encode("utf-8",errors="replace")+b"\n")
except Exception:
    print(out.encode("ascii",errors="replace").decode("ascii"))
s.close()
