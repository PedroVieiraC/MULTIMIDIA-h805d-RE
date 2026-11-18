#!/bin/sh
# Laco da home propria (docs/launcher/ETAPAS.md, etapa 6). Chamado pelo /data/boot.sh; pode ser relancado a mao.
# cdr fica VIVO por tras (LD_PRELOAD /data/touchfix.so: tela fantasma + filtro do touch + botao auxiliar).
#   /tmp/cdr_front existe = cdr na tela; senao a nossa home esta na frente e o cdr desenha num buffer fantasma.
#   /tmp/ui_toggle = pedido de troca (duplo power, Home/Som do botao auxiliar).
# O vigia (--btn) alimenta o /dev/watchdog: home e cdr rodam juntos e nenhum dos dois pode segurar sozinho.
restart_cdr() {
    killall -9 cdr lylinkapp 2>/dev/null
    while pidof cdr >/dev/null; do sleep 1; done
    killall gocsdk mdnsd hostapd udhcpd 2>/dev/null; sleep 1
    rm -f /tmp/cdr_front
    if [ -f /data/touchfix.so ]; then
        LD_PRELOAD=/data/touchfix.so /usr/bin/cdr >/dev/null 2>&1 &
    else
        touch /tmp/cdr_front; /usr/bin/cdr >/dev/null 2>&1 &   # sem o .so nao ha fantasma: cdr sempre na frente
    fi
}
mkdir -p /var/run/hostapd; touch /var/run/udhcpd.leases
/data/h805d-ui --btn >>/tmp/ui.log 2>&1 &
restart_cdr
while true; do
    rm -f /tmp/cdr_front; usleep 100000
    /data/h805d-ui >>/tmp/ui.log 2>&1      # sai no card (CarPlay/AA/Registro) ou no pedido de troca
    pidof cdr >/dev/null || restart_cdr
    rm -f /tmp/ui_toggle
    touch /tmp/cdr_front
    while [ ! -f /tmp/ui_toggle ]; do usleep 200000; done
done
