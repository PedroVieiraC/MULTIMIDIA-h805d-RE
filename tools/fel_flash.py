#!/usr/bin/env python3
# Runner de gravacao FEL EM LOTE, com log e verificacao por hash.
# Le tools/fel_plan.txt (linhas "<offset_hex> <arquivo>"), e para cada item:
# faz backup total (1x), grava, le de volta e compara sha256. Para no 1o erro.
# Tudo vai para logs/fel-AAAAMMDD-HHMMSS.log, pra achar onde parou se falhar.
#
# Pre-requisito: aparelho em FEL (via microSD) e USB-C conectado.
# Uso:  python tools/fel_flash.py           (le tools/fel_plan.txt)
#       XFEL=C:\caminho\xfel.exe python tools/fel_flash.py   (xfel customizado)
import os, sys, time, hashlib, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAN = os.path.join(ROOT, "tools", "fel_plan.txt")
LOGDIR = os.path.join(ROOT, "logs")
FLASH_SIZE = 16 * 1024 * 1024
def _find_xfel():
    if os.environ.get("XFEL"):
        return os.environ["XFEL"]
    rel = ("tools", "fel", "xfel-windows-v1.4.0", "xfel-windows-v1.4.0", "xfel.exe")
    # tenta o novo local (root_MD) e o antigo (MULTIMIDIA2/H805D-RESEARCH-CLAUDE-STRUCTURE)
    bases = [os.path.join(ROOT, "..", "root_MD"),
             os.path.join(ROOT, "..", "MULTIMIDIA2", "H805D-RESEARCH-CLAUDE-STRUCTURE")]
    for base in bases:
        p = os.path.join(base, *rel)
        if os.path.exists(p):
            return p
    return os.path.join(bases[0], *rel)  # fallback p/ msg de erro clara
XFEL = _find_xfel()

os.makedirs(LOGDIR, exist_ok=True)
LOG = os.path.join(LOGDIR, "fel-" + time.strftime("%Y%m%d-%H%M%S") + ".log")
_logf = open(LOG, "w", encoding="utf-8")

def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line); _logf.write(line + "\n"); _logf.flush()

def xfel(*args, timeout=600):
    log("xfel " + " ".join(args))
    r = subprocess.run([XFEL, *args], capture_output=True, text=True, timeout=timeout)
    tail = (r.stdout or "").strip().splitlines()[-1:] + (r.stderr or "").strip().splitlines()[-1:]
    for t in tail:
        log("   " + t)
    if r.returncode != 0:
        log("   !! xfel retornou %d" % r.returncode)
    return r.returncode == 0

def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

def read_plan():
    itens = []
    if not os.path.exists(PLAN):
        log("SEM tools/fel_plan.txt — nada a gravar."); return itens
    for ln in open(PLAN, encoding="utf-8"):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        off, arq = ln.split(None, 1)
        itens.append((int(off, 16), os.path.join(ROOT, arq.strip())))
    return itens

def main():
    log("=== FEL flash runner ===  xfel=%s" % XFEL)
    if not xfel("version"):
        log("ABORT: aparelho nao esta em FEL (xfel version falhou)."); return 1

    itens = read_plan()
    if not itens:
        log("Plano vazio. Saindo sem gravar."); return 0
    log("Itens no plano: " + ", ".join("%#x<-%s" % (o, os.path.basename(f)) for o, f in itens))
    for off, arq in itens:
        if not os.path.exists(arq):
            log("ABORT: arquivo nao existe: " + arq); return 1

    # backup total antes de qualquer escrita
    bkp = os.path.join(LOGDIR, "predump-" + time.strftime("%Y%m%d-%H%M%S") + ".bin")
    log("Backup total 16MB -> " + bkp)
    if not xfel("spinor", "read", "0", str(FLASH_SIZE), bkp):
        log("ABORT: backup falhou — NAO gravar sem backup."); return 1
    log("backup sha256=" + sha(bkp))

    # grava e verifica cada item
    for off, arq in itens:
        size = os.path.getsize(arq)
        want = sha(arq)
        log("--- GRAVANDO %#x <- %s (%d bytes, sha=%s)" % (off, arq, size, want))
        if not xfel("spinor", "write", hex(off), arq):
            log("ABORT: write falhou em %#x" % off); return 1
        rb = tempfile.NamedTemporaryFile(delete=False, suffix=".bin").name
        if not xfel("spinor", "read", hex(off), str(size), rb):
            log("ABORT: readback falhou em %#x" % off); return 1
        got = sha(rb)
        os.unlink(rb)
        if got == want:
            log("   OK verificado (%#x)" % off)
        else:
            log("   !! DIVERGIU em %#x: want=%s got=%s" % (off, want, got))
            log("ABORT: verificacao falhou — restaure com o backup se necessario."); return 1

    log("=== TUDO GRAVADO E VERIFICADO. Pode reiniciar (remover FEL). ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())
