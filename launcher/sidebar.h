#pragma once
#include "lvgl/lvgl.h"
void sidebar_create(lv_obj_t *scr);
void sidebar_close_all(void);   /* fecha popup e settings */
void sidebar_open_som(void);
void sidebar_open_config(void);    /* abre o popup de volume */
