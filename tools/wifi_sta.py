#!/usr/bin/env python3
# Liga/desliga a conexao do aparelho (wlan1 station) a uma rede Wi-Fi externa.
# Tudo em RAM no aparelho: um reboot da multimidia reverte sozinho.
# Aponta para o AP (192.168.10.1), que existe sempre -> o 'off' funciona mesmo
# depois da station cair. O AP/CarPlay (wlan0) nunca e tocado por este script.
#
# Uso:
#   python tools/wifi_sta.py status
#   python tools/wifi_sta.py on "SSID" "SENHA"
#   python tools/wifi_sta.py off
import socket, time, sys

# AP (quando Wi-Fi off) e rede de casa (quando Wi-Fi on move o AP de canal).
HOSTS = ["192.168.10.1", "192.168.1.100"]

def run(cmd):
    s = None
    for host in HOSTS:
        try:
            s = socket.socket(); s.settimeout(6)
            s.connect((host, 2323)); break
        except Exception:
            try: s.close()
            except Exception: pass
            s = None
    if s is None:
        return "ERRO: aparelho nao encontrado em %s (porta 2323)" % HOSTS
    time.sleep(0.4)
    s.sendall(cmd.encode() + b"\necho '===END==='\n"); time.sleep(1.2)
    data = b""
    try:
        while True:
            d = s.recv(8192)
            if not d: break
            data += d
            if b"===END===" in data: break
    except Exception:
        pass
    s.close()
    return data.decode(errors="replace").split("===END===")[0]

def status():
    return run("wpa_cli -i wlan1 status 2>/dev/null | grep -E 'wpa_state|^ssid|ip_address'; "
               "echo '--IP--'; ifconfig wlan1 2>/dev/null | grep 'inet addr'")

def on(ssid, psk):
    # config so em /tmp (RAM). psk vem por argumento, nao fica no repo.
    conf = ('ctrl_interface=/var/run/wpa_supplicant\\nupdate_config=1\\n'
            'network={{\\n ssid=\"{}\"\\n psk=\"{}\"\\n}}\\n').format(ssid, psk)
    # /data/wifi.conf = chave persistente (o watchdog /data/wifi_up.sh mantem conectado).
    cmd = ("printf '%s' > /data/wifi.conf; chmod 600 /data/wifi.conf; " % conf +
           "ifconfig wlan1 up; "
           "ps | grep -q '[w]ifi_up.sh' || setsid /bin/sh /data/wifi_up.sh >/dev/null 2>&1 </dev/null & "
           "killall wpa_supplicant 2>/dev/null; sleep 1; "
           "wpa_supplicant -B -i wlan1 -c /data/wifi.conf -D nl80211 >/dev/null 2>&1; "
           "sleep 3; udhcpc -i wlan1 -b >/dev/null 2>&1; "
           "sleep 1; wpa_cli -i wlan1 status 2>/dev/null | grep -E 'wpa_state|^ssid'; "
           "ifconfig wlan1 2>/dev/null | grep 'inet addr' || echo '(pegando IP... rode status em 3s)'")
    return run(cmd)

def off():
    # remove a chave (watchdog encerra e desconecta) + derruba agora. AP/CarPlay intactos.
    return run("rm -f /data/wifi.conf; killall wpa_supplicant 2>/dev/null; "
               "ifconfig wlan1 down; echo 'station OFF (chave removida, AP intacto)'")

def load_cfg():
    # le SSID e SENHA de tools/wifi_sta.cfg (2 linhas), arquivo local fora do git.
    import os
    p = os.path.join(os.path.dirname(__file__), "wifi_sta.cfg")
    try:
        linhas = [l.strip() for l in open(p, encoding="utf-8") if l.strip() and not l.startswith("#")]
        return linhas[0], linhas[1]
    except Exception:
        return None, None

def is_connected():
    return "wpa_state=COMPLETED" in status()

if __name__ == "__main__":
    a = sys.argv[1] if len(sys.argv) > 1 else "status"
    if a == "on":
        if len(sys.argv) >= 4:
            ssid, psk = sys.argv[2], sys.argv[3]
        else:
            ssid, psk = load_cfg()
        if not ssid:
            print("Sem credenciais. Use: wifi_sta.py on \"SSID\" \"SENHA\" ou crie tools/wifi_sta.cfg"); sys.exit(1)
        print(on(ssid, psk))
    elif a == "off":
        print(off())
    elif a == "toggle":
        # botao liga/desliga: conectado -> desliga; senao -> liga com as credenciais do .cfg
        if is_connected():
            print("Estava LIGADO -> desligando..."); print(off())
        else:
            ssid, psk = load_cfg()
            if not ssid:
                print("Sem tools/wifi_sta.cfg para ligar."); sys.exit(1)
            print("Estava DESLIGADO -> ligando em %s..." % ssid); print(on(ssid, psk))
    else:
        print(status())
