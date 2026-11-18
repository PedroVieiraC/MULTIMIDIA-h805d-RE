#!/bin/sh
# Instala a UI no aparelho e relanca o laco (derruba o CarPlay ~15 s). md5 conferido pelo push.py.
# O h805d-ui vai primeiro para /tmp (RAM): apagar o binario em uso nao libera espaco no jffs2 e o push direto trunca.
# A troca + relancamento e um comando so: sem o vigia o watchdog reinicia o aparelho em poucos segundos.
# NUNCA limpar /tmp/busybox nem /tmp/bb no aparelho: sao os comandos do shell de rede (rc.final copia de /bin/bb).
set -e
cd "$(dirname "$0")/.."
export MSYS_NO_PATHCONV=1
same() { # destino ja tem o mesmo arquivo: nao reenviar (o push apaga o destino antes; com o /data cheio isso ja perdeu o touchfix.so)
    [ "$(python tools/rsh.py "md5sum $2" 2>/dev/null | grep -oE '[0-9a-f]{32}' | head -1)" = "$(md5sum "$1" | cut -c1-32)" ]
}
p() { # o envio as vezes chega truncado (visto 2x em 13/11): repete ate o md5 conferir; arquivo critico nao fica pela metade
    same "$1" "$2" && { echo "igual, nao reenviado: $2"; return 0; }
    for i in 1 2 3 4 5; do python tools/push.py "$@" && return 0; sleep 2; done
    echo "FALHOU: $1 (destino pode estar truncado!)"; return 1
}
p launcher/touchfix.so /data/touchfix.so
p tools/boot.sh /data/boot.sh +x
p tools/ui_loop.sh /data/ui_loop.sh
p tools/sd_clean.sh /data/sd_clean.sh +x
p tools/act.sh /data/act.sh +x
python tools/rsh.py 'sh -n /data/boot.sh && sh -n /data/ui_loop.sh && sh -n /data/sd_clean.sh && sh -n /data/act.sh || { echo ERRO_SINTAXE_NO_APARELHO; exit 1; }' | grep -q ERRO_SINTAXE && { echo "ERRO: script com sintaxe invalida no aparelho, abortando antes de reiniciar"; exit 1; }
p launcher/h805d-ui.upx /tmp/h805d-ui.new +x
SUM=$(md5sum launcher/h805d-ui.upx | cut -c1-32)
# o cp para o jffs2 ja falhou por falta de espaco transitoria (binario truncado = Bus error): confere o md5 e repete
python tools/rsh.py "kill \$(pgrep -f ui_loop.sh); killall h805d-ui; for i in 1 2 3; do rm -f /data/h805d-ui; sync; sleep 4; cp /tmp/h805d-ui.new /data/h805d-ui && chmod +x /data/h805d-ui; [ \"\$(md5sum /data/h805d-ui | cut -c1-32)\" = $SUM ] && break; done; [ \"\$(md5sum /data/h805d-ui | cut -c1-32)\" = $SUM ] && rm -f /tmp/h805d-ui.new && echo MD5_OK || echo MD5_FALHOU; setsid sh /data/ui_loop.sh >/dev/null 2>&1 &"
