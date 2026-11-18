#pragma once
#include "lvgl/lvgl.h"
void settings_open(lv_obj_t *scr);
void settings_close(void);
extern const lv_font_t font_acentos_28; /* Montserrat 28 + acentos pt-BR, fallback na embutida */
extern const lv_font_t font_relogio_84; /* so 0-9 e ':' (comprimida, ~5 KB) */
void saver_init(void);                  /* protecao de tela (opcao 6); chamar uma vez na home */
