#!/bin/sh
# Compila h805d-ui (ARMv7, musl estático) num Alpine armv7 emulado. Rodar no Git Bash: sh build.sh
cd "$(dirname "$0")"
[ -d third_party/lvgl ] || git clone -q --depth 1 -b v9.2.2 https://github.com/lvgl/lvgl.git third_party/lvgl
REV=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)
MSYS_NO_PATHCONV=1 docker run --rm --platform linux/arm/v7 -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src -e REV=$REV alpine:3.24 \
  sh -c 'apk add -q gcc musl-dev make linux-headers && make -j$(nproc) GIT_REV=$REV h805d-ui && file h805d-ui 2>/dev/null; ls -l h805d-ui'
# /data (jffs2, pouco espaco) nao comporta o binario cru (~260 KB): instalar o h805d-ui.upx (UPX, ~174 KB)
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/w" alpine:3.20 \
  sh -c 'apk add -q upx >/dev/null && rm -f /w/h805d-ui.upx && upx -q --lzma --best -o /w/h805d-ui.upx /w/h805d-ui >/dev/null && ls -l /w/h805d-ui.upx'
