"""Toca na tela do aparelho (injeta eventos em /dev/input/event0, mesmo protocolo do touchfix). Uso: python tools/tap.py X Y [X Y ...]"""
import os, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
def ev(t, c, v): return struct.pack('<iiHHi', 0, 0, t, c, v)
def frames(x, y, down):
    if down:
        return (ev(3, 0x35, x) + ev(3, 0x36, y) + ev(3, 0x3a, 50) + ev(0, 2, 0) + ev(0, 0, 0)) * 3
    return ev(3, 0x3a, 0) + ev(0, 2, 0) + ev(0, 0, 0) + ev(0, 2, 0) + ev(0, 0, 0)
def run(*a): return subprocess.run([sys.executable, os.path.join(HERE, *a[:1]), *a[1:]], env=dict(os.environ, MSYS_NO_PATHCONV='1'), capture_output=True, text=True)
# arrastar: python tools/tap.py swipe X1 Y1 X2 Y2  (eventos gerados no aparelho com printf, um passo a cada 40 ms)
def _esc(b): return ''.join(chr(92) + 'x%02x' % c for c in b)
if sys.argv[1:2] == ['swipe']:
    x1, y1, x2, y2 = map(int, sys.argv[2:6])
    steps = 10
    cmd = []
    for k in range(steps + 1):
        x = x1 + (x2 - x1) * k // steps; y = y1 + (y2 - y1) * k // steps
        cmd.append("printf '%s' > /dev/input/event0; usleep 40000" % _esc(frames(x, y, 1)[:80]))
    cmd.append("printf '%s' > /dev/input/event0" % _esc(frames(x2, y2, 0)))
    print(run('rsh.py', '; '.join(cmd)).stdout[-60:])
    sys.exit(0)

args = list(map(int, sys.argv[1:]))
for x, y in zip(args[::2], args[1::2]):
    d = tempfile.mkdtemp()
    for n, down in (('d', 1), ('u', 0)):
        open(os.path.join(d, n), 'wb').write(frames(x, y, down))
        for _ in range(5):
            r = run('push.py', os.path.join(d, n), '/tmp/tap_' + n)
            if 'OK' in r.stdout and 'DIVERGE' not in r.stdout: break
    r = run('rsh.py', 'cat /tmp/tap_d > /dev/input/event0; usleep 150000; cat /tmp/tap_u > /dev/input/event0')
    print('toque', x, y)
