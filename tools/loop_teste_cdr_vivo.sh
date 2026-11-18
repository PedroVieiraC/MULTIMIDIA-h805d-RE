# Laco de teste: cdr vivo por tras (touchfix.so) + home por cima. Vigia (--btn) ja rodando e alimentando o watchdog.
restart_cdr() { killall -9 cdr lylinkapp 2>/dev/null; while pidof cdr >/dev/null; do sleep 1; done; killall gocsdk mdnsd hostapd udhcpd 2>/dev/null; sleep 1; rm -f /tmp/cdr_front; LD_PRELOAD=/tmp/touchfix.so /usr/bin/cdr >/dev/null 2>&1 & }
mkdir -p /var/run/hostapd; touch /var/run/udhcpd.leases
restart_cdr
while true; do
  rm -f /tmp/cdr_front; usleep 100000
  /tmp/h805d-ui >>/tmp/loop_ui.log 2>&1
  pidof cdr >/dev/null || restart_cdr
  rm -f /tmp/ui_toggle
  touch /tmp/cdr_front
  while [ ! -f /tmp/ui_toggle ]; do usleep 200000; done
done
