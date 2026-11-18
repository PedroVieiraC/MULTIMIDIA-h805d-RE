"""Cliente da API HTTP interna do H805D (servidor Civetweb :8082, modo 'WIFI Video').
Uso:
  python tools/api.py get                 # GET /api/getdeviceinfo
  python tools/api.py get "cmd=1"         # GET com querystring
  python tools/api.py raw /api/getdeviceinfo?cmd=1
  python tools/api.py probe               # so testa se a 8082 responde
Host: env H805D, senao tenta 192.168.1.100 (rede casa) e 192.168.10.1 (AP)."""
import sys,os,urllib.request,urllib.error

PORT=8082
hosts=[os.environ["H805D"]] if os.environ.get("H805D") else ["192.168.1.100","192.168.10.1"]

def fetch(path):
    last=None
    for h in hosts:
        url="http://%s:%d%s"%(h,PORT,path)
        try:
            # tenta sem auth; se 401, tenta Digest admin/admin e admin/(vazio)
            opener=urllib.request.build_opener()
            req=urllib.request.Request(url,headers={"User-Agent":"h805d-tool"})
            with opener.open(req,timeout=5) as r:
                return h,r.status,r.read()
        except urllib.error.HTTPError as e:
            if e.code==401:
                for u,p in [("admin","admin"),("admin",""),("root","root")]:
                    mgr=urllib.request.HTTPPasswordMgrWithDefaultRealm()
                    mgr.add_password(None,url,u,p)
                    op=urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(mgr))
                    try:
                        with op.open(url,timeout=5) as r:
                            return h,r.status,r.read()
                    except urllib.error.HTTPError as e2:
                        last="401 (auth %s/%s falhou)"%(u,p)
                    except Exception as e2:
                        last=str(e2)
            else:
                last="HTTP %d"%e.code; return h,e.code,e.read()
        except Exception as e:
            last=str(e)
    return None,None,("sem resposta na 8082 (%s). O modo 'WIFI Video' esta ativo na tela? erro: %s"%(hosts,last)).encode()

if __name__=="__main__":
    a=sys.argv[1] if len(sys.argv)>1 else "get"
    if a=="probe": path="/api/getdeviceinfo"
    elif a=="raw": path=sys.argv[2]
    elif a=="get":
        qs=("?"+sys.argv[2]) if len(sys.argv)>2 else ""
        path="/api/getdeviceinfo"+qs
    else: path=a
    host,status,body=fetch(path)
    print("host=%s status=%s"%(host,status))
    try: print(body.decode(errors="replace"))
    except: print(repr(body))
