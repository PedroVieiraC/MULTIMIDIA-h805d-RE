#include <stdint.h>
#include <unistd.h>
#include "home_ui.h"
#include "icons.h"
#include "sidebar.h"
#include "settings.h"

#define BAR_W 90
static lv_obj_t *s_bar, *s_back, *s_pop, *s_sl, *s_pct;
static int s_kind; /* 0 nenhum, 1 som, 2 brilho */

static void pop_close(void)
{
    if (s_pop) lv_obj_delete_async(s_pop);
    if (s_back) lv_obj_delete_async(s_back);
    s_pop = s_back = NULL;
    s_kind = 0;
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    pop_close();
}

/* d == 0: veio do slider */
static void vol_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = lv_slider_get_value(s_sl) + d;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    if (d) lv_slider_set_value(s_sl, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_pct, "%d%%", v);
    if (s_kind == 1) hw_volume_set(v);
    else if (s_kind == 2) hw_brightness_set(v < 10 ? 10 : v); /* piso 10% */
}

static void pop_open(int kind, int y)
{
    int same = s_kind == kind;
    pop_close();
    if (same) return;
    s_kind = kind;
    s_back = lv_obj_create(lv_screen_active()); /* toque fora fecha */
    lv_obj_set_pos(s_back, BAR_W, 0);
    lv_obj_set_size(s_back, 1600 - BAR_W, 600);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_back, 0, 0);
    lv_obj_remove_flag(s_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_back, close_cb, LV_EVENT_CLICKED, NULL);

    s_pop = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s_pop, BAR_W + 10, y);
    lv_obj_set_size(s_pop, 460, 130);
    lv_obj_set_style_bg_color(s_pop, lv_color_hex(PANEL), 0);
    lv_obj_set_style_radius(s_pop, 24, 0);
    lv_obj_set_style_border_width(s_pop, 0, 0);
    lv_obj_set_style_pad_all(s_pop, 0, 0);
    lv_obj_remove_flag(s_pop, LV_OBJ_FLAG_SCROLLABLE);

    int som = kind == 1;
    int v = som ? hw_volume_get() : hw_brightness_get();
    mk_lbl(s_pop, som ? "Volume" : "Brilho", 20, 4, MUTED);
    s_pct = mk_lbl(s_pop, "", 330, 4, MUTED);
    lv_obj_set_width(s_pct, 110);
    lv_obj_set_style_text_align(s_pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(s_pct, "%d%%", v);
    lv_obj_t *m = mk_btn(s_pop, LV_SYMBOL_MINUS, 10, 40, 80, 80, BTN, vol_cb, (void *)(intptr_t)-10);
    lv_obj_t *p = mk_btn(s_pop, LV_SYMBOL_PLUS, 370, 40, 80, 80, BTN, vol_cb, (void *)(intptr_t)10);
    s_sl = lv_slider_create(s_pop);
    lv_obj_set_pos(s_sl, 126, 60); /* folga p/ o knob (pad 16) nao encostar no - e + */
    lv_obj_set_size(s_sl, 218, 40);
    lv_slider_set_range(s_sl, som ? 0 : 10, 100);
    lv_obj_set_style_bg_color(s_sl, lv_color_hex(BTN), 0);
    lv_obj_set_style_radius(s_sl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sl, lv_color_hex(ACC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_sl, lv_color_hex(WHITE), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_sl, 16, LV_PART_KNOB);
    lv_obj_set_style_radius(s_sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_add_event_cb(s_sl, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_slider_set_value(s_sl, v, LV_ANIM_OFF);
    if (!som && access("/data/bright_ok", F_OK)) { /* brilho so com /data/bright_ok (ver hw_brightness_set no main.c) */
        lv_obj_add_state(m, LV_STATE_DISABLED);
        lv_obj_add_state(p, LV_STATE_DISABLED);
        lv_obj_add_state(s_sl, LV_STATE_DISABLED);
    }
    lv_obj_move_foreground(s_bar);
}

static void bar_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == 0) {
        sidebar_close_all();
    } else if (i == 3) {
        pop_close();
        settings_open(lv_screen_active());
        lv_obj_move_foreground(s_bar);
    } else
        pop_open(i, i * 150 + 75 - 65); /* centrado no botao */
}

void sidebar_open_config(void) /* depois de trocar o tema: volta para as Configuracoes */
{
    pop_close();
    settings_open(lv_screen_active());
    lv_obj_move_foreground(s_bar);
}

void sidebar_open_som(void) { pop_open(1, 160); } /* botao auxiliar do CarPlay -> "Som" */

void sidebar_close_all(void)
{
    pop_close();
    settings_close();
}

void sidebar_create(lv_obj_t *scr)
{
    static const lv_image_dsc_t *const ic[4] = {&icon_home, &icon_volume, &icon_light, &icon_set};
    s_bar = lv_obj_create(scr);
    lv_obj_set_size(s_bar, BAR_W, 600);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(BARC), 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 0, 0);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = mk_btn(s_bar, "", 0, i * 150, BAR_W, 150, BARC, bar_cb, (void *)(intptr_t)i);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_t *im = lv_image_create(b);
        lv_image_set_src(im, ic[i]);
        lv_obj_set_style_image_recolor(im, lv_color_hex(ACC), 0);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_center(im);
    }
}
