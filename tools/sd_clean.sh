#!/bin/sh
# Apaga as gravacoes mais antigas do SD quando falta espaco (o cdr so pede para formatar o cartao).
# Liga por padrao; desligar: touch /data/sd_loop_off. Mexe so em /mnt/extsd/video/{F,R} (*.ts + miniatura *_ths.jpg);
# event/ park/ photo/ ficam intactos. Comeca a apagar com < LOW KB livres e para com >= HIGH; nunca apaga as 6 mais novas.
D=/mnt/extsd/video; LOW=4194304; HIGH=6291456   # 4 GB / 6 GB
free() { df -k /mnt/extsd 2>/dev/null | tail -1 | awk '{print $(NF-2)}'; }
while :; do
    sleep 30
    [ -f /data/sd_loop_off ] || [ ! -d $D ] && continue
    f=$(free); [ -n "$f" ] && [ "$f" -lt $LOW ] || continue
    for d in F R; do ls -1 $D/$d 2>/dev/null | grep '\.ts$' | sed "s|^|$d |"; done | sort -k2 > /tmp/sdl
    n=$(wc -l < /tmp/sdl); [ "$n" -gt 6 ] || continue
    head -n $((n - 6)) /tmp/sdl | while read d x; do
        rm -f "$D/$d/$x" "$D/$d/${x%.ts}_ths.jpg"
        [ "$(free)" -ge $HIGH ] && break
    done
done
