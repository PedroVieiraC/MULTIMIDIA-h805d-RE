#!/bin/sh
# touchfix.so (LD_PRELOAD do cdr). Sem libc no link: os simbolos vem do libc.so do aparelho em tempo de execucao.
cd "$(dirname "$0")"
MSYS_NO_PATHCONV=1 docker run --rm --platform linux/arm/v7 -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src alpine:3.24 sh -c \
  'apk add -q gcc musl-dev binutils && gcc -shared -fPIC -Os -mcpu=cortex-a7 -fno-stack-protector -U_FORTIFY_SOURCE -nostdlib -Wl,--hash-style=both -o touchfix.so touchfix.c -lgcc && strip touchfix.so && readelf -d touchfix.so | grep -E "NEEDED|RELR"; readelf --dyn-syms -W touchfix.so | awk "\$7==\"UND\"{print \$8}" | tr "\n" " "; echo; ls -l touchfix.so'
