# Sistema nativo do H805D — mapa da organização (sem CarPlay)

> Abstração de **como a multimídia funciona como aparelho/sistema**, separada do CarPlay.
> Levantado ao vivo (root pela rede) em 2026-11-11. O software foi puxado pro PC em
> `artifacts/device_native/` (fora do git — regenerável; ver "Como puxar" no fim).

## 1. Quem é o fabricante
- Marca de anúncio: ESSGOO / GOODOCOM. **Fabricante real (firmware): `LIANGCAI`** (string `"sp":"LIANGCAI"` no binário).
- Nome interno do produto: **`V536-CDR`** / `F900` (ver `version="F900"` no `menu.cfg`).
- **Não é Android.** É Linux (Tina/OpenWrt) com UI própria. Nada de launcher/APK — os guias de "head unit Android" **não se aplicam**. Não há firmware público; o que temos é o que puxamos do próprio aparelho.

## 2. O coração: um único app `/usr/bin/cdr`
Tudo roda dentro de **um binário monolítico** `/usr/bin/cdr` (7,6 MB, ARM musl, **stripped**).
Threads dele (de `/proc/<pid>/task`): `gui`, `VICapture`, `stateMng`, `isp_thread`, `VEncChn0`,
`AOChn`, `netManagerThread`, `gocsdkCheck`, `previewthd`. Ou seja, o mesmo processo faz:
**menu/GUI + captura de câmera (DVR) + encoder + áudio + rede + checagem do CarPlay.**

Bibliotecas que ele carrega:
| Lib | Papel |
|-----|-------|
| `liblylinkui.so` (50 KB) | UI do espelhamento (lylink) |
| `liblylinkmw.so` / `server` / `client` / `usb` | middleware do espelhamento |
| `liblylinkaa.so` (513 KB) | Android Auto |
| `liblylog.so`, `libglog.so.0` | logs |
| `libasound.so.2` | áudio (ALSA) |

> **CarPlay/Android Auto/HiCar/AirPlay** = o subsistema **lylink** + o processo `gocsdk`
> (+ `/data/lylink/carplay.key`, a chave de autenticação — segredo do aparelho, **não sai do device**).
> É a parte que você quer deixar de lado por enquanto.

## 3. Como a interface é construída (o que dá pra mexer)
- **Framework gráfico: LVGL** (open-source, bem documentado — `lv_img_class`, `lv_img_decoder_*`,
  `lv_animimg_class` no binário). A tela é composta por objetos LVGL + imagens PNG.
- **Assets = arquivos no rootfs**, não embutidos no código:
  - `/usr/share/images/` → **188 PNGs**, toda a arte (referenciados no código como `P:/usr/share/images/...`).
  - `/usr/share/osd/fonts/` + `/usr/share/osd/images/` → fontes e ícones do OSD (texto sobre vídeo).
- **Grupos de UI** (por prefixo do nome dos PNGs):
  | Prefixo | Tela/área |
  |---------|-----------|
  | `dsk_*` (31) | **Desktop/home** — os "apps" da tela inicial |
  | `set_*` (23) | telas de **Configurações** |
  | `ctlB_*` (17) | **barra de controle** (back/home/luz/volume/set) |
  | `topbar_*` (11) | barra de status de cima |
  | `sta_*` (16) | ícones de status |
  | `pb_*`/`mp_*`/`pv_*` | playback / media player / preview (DVR) |
  | `fm_*`, `music_*`, `btmng_*`, `ao_*`, `car_*`, `time_*` | FM, música, BT manage, saída de áudio, câmera, relógio |
- **Apps da home** (`dsk_*`): preview (DVR), album/playback, audio, bt_music, fm, miracast,
  airplay, auto (Android Auto), cp (CarPlay), hc (HiCar), cf (CarLife), wifi_video.
- **Trocar a aparência do menu = trocar PNGs em `/usr/share/images/`** (mesmo nome/tamanho).
  Como `/` é squashfs **read-only**, mudança **permanente** exige FEL; dá pra **prototipar** antes.

## 4. Configuração do aparelho (todos os ajustes)
- **Defaults (texto):** `/usr/share/configs/menu.cfg` — ~44 parâmetros: resolução de gravação,
  tempo de clipe, timelapse, idioma (`systemLanguage`), fuso, volume, **brilho com agenda dia/noite**
  (`brightnessnightstart/end`), G-sensor, estacionamento, SSID/senha do AP, FM, etc. (cópia em
  `artifacts/device_native/usr/share/configs/menu.cfg`).
- **Estado vivo (binário):** **`/data/userConfig.bin`** (308 B) — é o que o app lê/grava de verdade.
  Formato ainda não decodificado (308 B ≈ struct empacotada dos ~44 campos). **Editar isso = mudar
  as configurações sem FEL** (é gravável), mas precisa mapear o formato antes (próximo passo candidato).
- Outros: `/data/bt_conf.ini` (nome BT), `/data/lylink/bt.mac`, `/data/.goc_database`, `/data/sc_db.db`.

## 5. A API de controle (o jeito limpo de "mexer no sistema")
O `cdr` embute um **servidor HTTP Civetweb** (não-Android), com **Digest auth**, que serve JSON:
- Endpoints vistos: **`/api/getdeviceinfo`**, **`/api/setdeviceinfo`**.
- Respostas/estruturas: `{"device_id":0,"device_name":"V536-CDR","software":"V1.0",...}`,
  **`{"Menu":[{"Cmd":N,"MenuList":[{"Id":"..","Index":N}],"Name":".."}]}`** (a estrutura do menu
  de configurações), **`{"List":[{"fpath","name","time","type","size"}],"total":N}`** (gravações do DVR),
  `{"Cmd":N,"Value":N}` (setar um ajuste).
- Porta: **8082** (`listening_ports=8082`). É assim que o **app-companheiro do celular** controla o aparelho.
- ⚠️ **Hoje a 8082 NÃO está ouvindo** (só 2323=nosso bind-shell e 5037=adb). O servidor sobe sob
  condição (provável: ao conectar o app do celular / entrar no modo de download por Wi-Fi). Descobrir
  o gatilho e falar essa API pela rede é o caminho mais limpo pra dirigir o aparelho — **próximo passo**.

## 6. Resumo da pilha
```
Hardware (Allwinner V851/V853, câmeras, tela touch gt911, RTC, G-sensor)
      │
Linux 4.9 (Tina) ── rootfs squashfs RO  +  /data jffs2 RW
      │
/usr/bin/cdr  (monólito)
  ├─ GUI: LVGL  ──► assets /usr/share/images/*.png (188) + osd fonts
  ├─ DVR: VICapture/ISP/VEnc ──► gravações em /mnt/UDISK
  ├─ Config: menu.cfg (default) + /data/userConfig.bin (vivo)
  ├─ API: Civetweb :8082  /api/getdeviceinfo /api/setdeviceinfo  (menu, filelist)
  └─ Espelhamento: lylink + gocsdk  ◄── CarPlay/AndroidAuto/HiCar/AirPlay (parte "carro")
```

## 7. O que dá pra fazer com isso (sem FEL, pela rede)
1. **Dirigir pela API Civetweb** (quando a 8082 subir): ler/alterar ajustes, listar/baixar gravações.
2. **Mudar ajustes via `/data/userConfig.bin`** (gravável) — depois de decodificar o formato.
3. **Prototipar visual** trocando PNGs (permanente só via FEL — acumular no backlog).
4. **Hooks de boot** em `/data/boot.sh` (delegador já gravado) pra automatizar qualquer coisa acima.

## 8. Como puxar o software de novo (está fora do git)
```
set MSYS_NO_PATHCONV=1   (Git Bash)
python tools/pull.py /usr/bin/cdr            artifacts/device_native/usr/bin/cdr
python tools/pull.py /data/userConfig.bin    artifacts/device_native/data/userConfig.bin
python tools/pull_glob.py "/usr/share/images/*.png" artifacts/device_native/usr/share/images
```
`tools/pull.py` = 1 arquivo; `tools/pull_glob.py` = vários numa conexão (base64 pela porta 2323).
