"""Gera launcher/icons.c/.h (LV_COLOR_FORMAT_A8) a partir do canal alfa dos PNGs da barra lateral."""
from PIL import Image
names = ["home", "volume", "light", "set"]
c = ['#include "icons.h"', ""]
for n in names:
    im = Image.open(f"artifacts/ui_mod/ctlB_btn_{n}.png").convert("RGBA")
    w, h = im.size
    a = im.getchannel("A").tobytes()
    c.append(f"static const uint8_t d_{n}[{len(a)}] = {{")
    for i in range(0, len(a), 24):
        c.append(" " + ",".join(str(x) for x in a[i:i+24]) + ",")
    c.append("};")
    c.append(f"const lv_image_dsc_t icon_{n} = {{ .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, "
             f".cf = LV_COLOR_FORMAT_A8, .w = {w}, .h = {h}, .stride = {w} }}, .data_size = {len(a)}, .data = d_{n} }};")
    c.append("")
open("launcher/icons.c", "w", newline="\n").write("\n".join(c))
open("launcher/icons.h", "w", newline="\n").write(
    '#pragma once\n#include "lvgl/lvgl.h"\n/* gerado por tools/png2a8.py */\n'
    + "".join(f"extern const lv_image_dsc_t icon_{n};\n" for n in names))
