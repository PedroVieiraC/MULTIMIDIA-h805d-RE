#pragma once
#include "lvgl/lvgl.h"
/* implementadas no main.c */
int  hw_brightness_get(void);          /* 10..100 (%) */
void hw_brightness_set(int pct);       /* 10..100 (%) */
enum { AUDIO_SPK, AUDIO_FM, AUDIO_AUX };
void hw_audio_out(int mode);
void hw_fm_freq(int freq_10khz);       /* 7600..10800, ex.: 8800 = 88.0 MHz */
void hw_open_cdr_at(const char *xy); /* atalho: toca em "x y" na home do cdr e o traz para a frente; "!x y" = sai do CarPlay se preciso */
void hw_open_cdr(void);                /* sai da home e abre o sistema original (CarPlay/DVR) */
int  hw_volume_get(void);              /* 0..100 (%) */
void hw_volume_set(int pct);           /* 0..100 (%) */
/* hw_config.c: id 0..19 = ordem do menu de Configuracoes (REQUISITOS 2) */
int  hw_config_get(int id);
void hw_config_set(int id, int val);
int  hw_config_mapped(int id);       /* 1 = grava no userConfig.bin */
int  hw_config_test(const char *path); /* --cfgtest */
int  hw_config_do(int id);             /* acoes (limpar historico, formatar, fabrica); 0 = ok */
/* home_ui.c */
void home_ui_create(lv_obj_t *scr);
/* tema em tempo de execucao (home_ui.c: theme_apply). 22 = cor principal, 23 = cor secundaria (hw_config_get) */
typedef struct { uint32_t bg, card, btn, press, panel, bar, muted, text, acc; } Theme;
extern Theme TH;
#define BG      TH.bg
#define CARD    TH.card
#define BTN     TH.btn
#define PRESS   TH.press
#define ACC     TH.acc
#define PANEL   TH.panel
#define BARC    TH.bar
#define MUTED   TH.muted
#define WHITE   TH.text   /* cor do texto: branco nos escuros, preto nos claros */
void hw_restart_ui(void);              /* main.c: reabre a home (execv) e volta para as Configuracoes */
lv_obj_t *mk_btn(lv_obj_t *p, const char *txt, int32_t x, int32_t y, int32_t w, int32_t h,
                 uint32_t bg, lv_event_cb_t cb, void *ud);
lv_obj_t *mk_lbl(lv_obj_t *p, const char *txt, int32_t x, int32_t y, uint32_t color);
