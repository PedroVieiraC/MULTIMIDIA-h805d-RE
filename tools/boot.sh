#!/bin/sh
# Script de boot editavel pela rede (roda do rc.final via delegador generico).
# Adicione aqui o que quiser subir no boot. Sem reflash: basta editar este arquivo.

# Diagnostico de reboots no carro: uma linha por boot (data, uptime); mantem so os ultimos ~2 KB.
echo "$(date +%s) $(cut -d' ' -f1 /proc/uptime) $(grep -o 'bootreason=[^ ]*' /proc/cmdline | cut -d= -f2)" >> /data/boot.log
[ "$(wc -c < /data/boot.log)" -gt 4096 ] && { tail -c 2048 /data/boot.log > /tmp/boot.log.t; cat /tmp/boot.log.t > /data/boot.log; }

# Diagnostico do carro: grava so as MUDANCAS de acc/reverse (uptime, acc rev aux) em /data/gpio.log; para de gravar com 3 KB.
( p=""; while :; do s=$(echo $(cat /sys/class/gpio-det/acc/state /sys/class/gpio-det/reverse/state /sys/class/gpio-det/aux/state 2>/dev/null))
  if [ "$s" != "$p" ]; then p="$s"; [ "$(wc -c < /data/gpio.log 2>/dev/null || echo 0)" -lt 3000 ] && echo "$(cut -d' ' -f1 /proc/uptime) $s" >> /data/gpio.log; fi
  usleep 500000; done ) &

# Apaga as gravacoes antigas do SD quando enche (desligar: touch /data/sd_loop_off).
[ -f /data/sd_clean.sh ] && sh /data/sd_clean.sh &

# Wi-Fi station: sobe so se /data/wifi.conf existir (botao on cria, off remove).
[ -f /data/wifi.conf ] && /bin/sh /data/wifi_up.sh &

# Relanca o cdr do zero. Os servicos que ele sobe NAO morrem com ele: sem parar,
# o novo cdr cria um 2o gocsdk (Bluetooth) e o CarPlay some (visto 2026-11-11).
restart_cdr() {
    killall -9 cdr lylinkapp 2>/dev/null
    while pidof cdr >/dev/null; do sleep 1; done
    killall gocsdk mdnsd hostapd udhcpd 2>/dev/null; sleep 1
    # touchfix.so: filtra os "soltou" falsos do touch Goodix (arrasto no CarPlay/Waze). Tirar = apagar o .so.
    if [ -f /data/touchfix.so ]; then
        touch /tmp/cdr_front   # sem o laco da home: cdr sempre na tela (o .so so filtra o touch)
        LD_PRELOAD=/data/touchfix.so /usr/bin/cdr >/dev/null 2>&1 &
    else
        /usr/bin/cdr >/dev/null 2>&1 &
    fi
}

# --- Overrides de UI: bind de cada PNG em /data/ui/ sobre /usr/share/images/ ---
# Trocar/qualquer icone no futuro = so colocar o PNG (mesmo nome) em /data/ui/.
if [ -d /data/ui ]; then
    did=0
    for f in /data/ui/*.png; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        t="/usr/share/images/$b"
        if [ -f "$t" ] && ! mount | grep -q "images/$b\$"; then
            mount --bind "$f" "$t" && did=1
        fi
    done
    # cdr ja subiu (S70app) com os assets originais em cache -> recarrega com os binds
    # (se o launcher vai rodar, nao precisa: o cdr sobe do zero quando o launcher sair)
    if [ "$did" = 1 ] && [ ! -x /data/h805d-ui -o -f /data/ui_off ]; then
        restart_cdr
    fi
fi

# --- Launcher proprio (docs/launcher/ETAPAS.md) no lugar do cdr ---
# Desligar: touch /data/ui_off.  Anti-loop: se o boot anterior nao chegou a 30 s
# de launcher (travou -> watchdog reiniciou), este boot pula o launcher e sobe o cdr.
if [ -x /data/h805d-ui ] && [ ! -f /data/ui_off ]; then
    if [ -f /data/ui_pending ]; then
        rm -f /data/ui_pending
        restart_cdr
    else
        touch /data/ui_pending
        (sleep 30; rm -f /data/ui_pending) &
        # Mata o cdr do S70app (sobe sem touchfix.so) e entrega ao laco: cdr vivo por tras + home na frente.
        killall -9 cdr lylinkapp 2>/dev/null
        while pidof cdr >/dev/null; do sleep 1; done
        grep -q '^8733bs ' /proc/modules || insmod /lib/modules/$(uname -r)/8733bs.ko
        sh /data/ui_loop.sh
    fi
fi
