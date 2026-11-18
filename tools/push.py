"""Envia um arquivo do PC pra multimidia (bind-shell 2323) via 'base64 -d' por stdin.
Funciona pra qualquer tamanho (streaming, sem limite de argumento).
Uso: python tools/push.py <local> <device_path> [+x]   (+x = chmod +x no destino)
Host: env H805D, senao tenta 192.168.1.100 e 192.168.10.1."""
import socket,time,sys,os,base64,hashlib

if len(sys.argv)<3:
    sys.exit("uso: python tools/push.py <local> <device_path> [+x]")
local,remote=sys.argv[1],sys.argv[2]
exe=len(sys.argv)>3 and sys.argv[3]=="+x"
raw=open(local,"rb").read()
b64=base64.b64encode(raw)
md5=hashlib.md5(raw).hexdigest()
hosts=[os.environ["H805D"]] if os.environ.get("H805D") else ["192.168.1.100","192.168.10.1"]

def conn():
    for h in hosts:
        try:
            s=socket.socket(); s.settimeout(10); s.connect((h,2323)); return s
        except Exception: pass
    sys.exit("sem conexao (tentei: %s)"%", ".join(hosts))

# rm -f antes: jffs2 quase cheio trunca ao sobrescrever no lugar
# conexao 1: escreve via stdin do base64 -d
s=conn(); time.sleep(0.3)
rdir=os.path.dirname(remote) or "/"
s.sendall(("mkdir -p '%s'; rm -f '%s'; base64 -d > '%s'\n"%(rdir,remote,remote)).encode())
time.sleep(0.2)
s.sendall(b64)
try: s.shutdown(socket.SHUT_WR)
except Exception: pass
# drena ate fechar
s.settimeout(60)
try:
    while s.recv(8192): pass
except Exception: pass
s.close()
time.sleep(0.3)

# conexao 2: verifica tamanho e md5
s=conn(); time.sleep(0.3)
MK=b"===V==="
s.sendall(("%secho %s; wc -c < '%s'; md5sum '%s'\nexit\n"%("chmod +x '%s'; "%remote if exe else "",MK.decode(),remote,remote)).encode())
try: s.shutdown(socket.SHUT_WR)
except Exception: pass
s.settimeout(30); data=b""
try:
    while True:
        d=s.recv(8192)
        if not d: break
        data+=d
except Exception: pass
s.close()
out=data.split(MK)[-1].split()
dev_sz=out[0].decode() if out else "?"
dev_md5=out[1].decode() if len(out)>1 else "?"
ok = (dev_sz==str(len(raw))) and (dev_md5==md5)
print("push %s -> %s : local=%d md5=%s | device sz=%s md5=%s %s"
      %(local,remote,len(raw),md5,dev_sz,dev_md5,"OK" if ok else "<<< DIVERGE"))
sys.exit(0 if ok else 1)
