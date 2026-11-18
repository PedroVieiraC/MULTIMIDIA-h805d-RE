#!/bin/sh
# Acoes das Configuracoes, chamadas pela home (hw_config_do): act.sh clear_conn | format_sd
# Para o cdr (e os servicos dele) com a home na frente, mexe nos arquivos e sobe o cdr de novo ATRAS da home, como o ui_loop.
stop_cdr() {
    killall -9 cdr lylinkapp 2>/dev/null
    while pidof cdr >/dev/null; do sleep 1; done
    killall gocsdk mdnsd hostapd udhcpd 2>/dev/null; sleep 2
}
start_cdr() {
    rm -f /tmp/cdr_front
    if [ -f /data/touchfix.so ]; then LD_PRELOAD=/data/touchfix.so setsid /usr/bin/cdr >/dev/null 2>&1 &
    else setsid /usr/bin/cdr >/dev/null 2>&1 & fi
}
case "$1" in
clear_conn) # esquece os telefones pareados (Bluetooth) e o keychain do CarPlay; o iPhone tambem precisa esquecer o carro
    stop_cdr
    { echo btenable=1; grep '^localname=' /data/bt_conf.ini; echo pairlistindex=0,; echo pairlisttotal=0; } > /tmp/bt.new
    cat /tmp/bt.new > /data/bt_conf.ini; rm -f /tmp/bt.new /data/sc_db.db /data/.goc_database /data/lylink/carplay.key
    sync; start_cdr ;;
format_sd) # sem o format_flag.txt o cdr formata o cartao ao subir (newfs_msdos -F 32 ...); depois recoloca o papel de parede padrao
    stop_cdr
    rm -f /mnt/extsd/format_flag.txt; sync
    start_cdr
    i=0; while [ $i -lt 90 ]; do
        [ -d /mnt/extsd/video ] && [ -f /data/wallpaper_default.jpg ] && { mkdir -p /mnt/extsd/h805d; cp /data/wallpaper_default.jpg /mnt/extsd/h805d/wallpaper.jpg; break; }
        i=$((i + 1)); sleep 4
    done ;;
esac
