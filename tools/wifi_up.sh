#!/bin/sh
# Watchdog da station wlan1 da multimidia (vive em /data/wifi_up.sh no aparelho).
# Mantem a conexao enquanto /data/wifi.conf existir; o hook delegador no rc.final
# o inicia no boot. Editavel sem reflash. Botao 'off' remove /data/wifi.conf -> encerra.
while [ -f /data/wifi.conf ]; do
    /sbin/ifconfig wlan1 up 2>/dev/null
    if ! wpa_cli -i wlan1 status 2>/dev/null | grep -q 'wpa_state=COMPLETED'; then
        wpa_cli -i wlan1 status >/dev/null 2>&1 || /usr/sbin/wpa_supplicant -B -i wlan1 -c /data/wifi.conf -D nl80211 >/dev/null 2>&1
        sleep 6
        ifconfig wlan1 2>/dev/null | grep -q 'inet addr' || /sbin/udhcpc -i wlan1 -n -q >/dev/null 2>&1
    fi
    sleep 15
done
killall wpa_supplicant 2>/dev/null
/sbin/ifconfig wlan1 down 2>/dev/null
