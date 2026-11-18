"""Puxa um arquivo da multimidia (bind-shell 2323) pro PC via base64.
Uso: python tools/pull.py <caminho_no_device> <destino_local>
Host: env H805D, senao tenta 192.168.10.1 e 192.168.1.100."""
import socket,time,sys,os,base64

if len(sys.argv)<3:
    sys.exit("uso: python tools/pull.py <device_path> <local_dest>")
remote,dest=sys.argv[1],sys.argv[2]
hosts=[os.environ["H805D"]] if os.environ.get("H805D") else ["192.168.10.1","192.168.1.100"]
s=None
for h in hosts:
    try:
        s=socket.socket(); s.settimeout(8); s.connect((h,2323)); break
    except Exception: s=None
if s is None:
    sys.exit("sem conexao (tentei: %s)"%", ".join(hosts))
time.sleep(0.4)
START=b"===B64START==="; END=b"===B64END==="
s.sendall(b"echo %s; base64 '%s'; echo %s\nexit\n"%(START,remote.encode(),END))
try: s.shutdown(socket.SHUT_WR)  # device le EOF e encerra o shell, liberando o listener
except Exception: pass
s.settimeout(90)
data=b""
try:
    while END not in data:
        d=s.recv(65536)
        if not d: break
        data+=d
except socket.timeout:
    sys.exit("timeout lendo %s (recebido %d bytes)"%(remote,len(data)))
s.close()
# pega so o que esta ENTRE o ultimo START (pula o eco do comando) e o END
if START not in data: sys.exit("marcador START nao veio; device respondeu?\n%r"%data[:200])
b64=data.rsplit(START,1)[1].split(END)[0]
b64=b"".join(b64.split())  # tira whitespace (inclui o \n apos o echo START)
try:
    raw=base64.b64decode(b64)
except Exception as e:
    sys.exit("falha decodificando: %s"%e)
os.makedirs(os.path.dirname(dest) or ".",exist_ok=True)
with open(dest,"wb") as f: f.write(raw)
print("ok: %s -> %s (%d bytes)"%(remote,dest,len(raw)))
