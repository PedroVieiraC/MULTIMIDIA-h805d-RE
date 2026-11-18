import sys
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    c = 0xFFFFFFFF
    for y in d[4:308]:
        c ^= y << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7 if c & 0x80000000 else c << 1) & 0xFFFFFFFF
    ok = len(d) == 308 and int.from_bytes(d[:4], 'little') == c
    print(p, len(d), 'CRC ok' if ok else 'CRC ERRO (calc %08x)' % c)
