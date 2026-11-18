"""Puxa varios arquivos da multimidia numa conexao so (base64 com marcadores).
Uso: python tools/pull_glob.py "<glob_no_device>" <dir_local_base>
Ex:  python tools/pull_glob.py "/usr/share/images/*.png" artifacts/device_native/usr/share/images
Preserva o basename de cada arquivo em <dir_local_base>."""
import socket,time,sys,os,base64,re

if len(sys.argv)<3:
    sys.exit('uso: python tools/pull_glob.py "<glob>" <dir_local>')
glob,outdir=sys.argv[1],sys.argv[2]
hosts=[os.environ["H805D"]] if os.environ.get("H805D") else ["192.168.10.1","192.168.1.100"]
s=None
for h in hosts:
    try:
        s=socket.socket(); s.settimeout(8); s.connect((h,2323)); break
    except Exception: s=None
if s is None: sys.exit("sem conexao (tentei: %s)"%", ".join(hosts))
time.sleep(0.4)
ALLEND=b"===ALLEND==="
cmd=b'for f in %s; do echo "===FILE:$f==="; base64 "$f"; done; echo %s\n'%(glob.encode(),ALLEND)
s.sendall(cmd)
s.settimeout(120)
data=b""
try:
    while ALLEND not in data:
        d=s.recv(65536)
        if not d: break
        data+=d
except socket.timeout:
    print("aviso: timeout, processando %d bytes recebidos"%len(data))
s.close()
data=data.split(ALLEND)[0]
# divide em blocos por ===FILE:path===
parts=re.split(rb"===FILE:(.*?)===\n",data)
# parts[0] = lixo antes do 1o marcador; depois alterna [path, conteudo, path, conteudo...]
os.makedirs(outdir,exist_ok=True)
n=0
for i in range(1,len(parts),2):
    path=parts[i].decode(errors="replace").strip()
    b64=b"".join(parts[i+1].split())
    if not b64:
        print("vazio/erro: %s"%path); continue
    try:
        raw=base64.b64decode(b64+b"="*(-len(b64)%4))
    except Exception as e:
        print("falha %s: %s"%(path,e)); continue
    dest=os.path.join(outdir,os.path.basename(path))
    with open(dest,"wb") as f: f.write(raw)
    n+=1
print("ok: %d arquivos -> %s"%(n,outdir))
