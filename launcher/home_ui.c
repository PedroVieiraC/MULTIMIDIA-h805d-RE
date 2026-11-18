#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define WALLPAPER "/mnt/extsd/h805d/wallpaper.jpg"
#define WALL_DATA "/data/wallpaper_default.jpg" /* copia leve 1600x600 q20 (o /data tem pouco espaco) */
#define WALL_DIM 0 /* % de preto sobre o papel de parede (0 = foto pura) */
#include "lvgl/lvgl.h"
#include "home_ui.h"
#include "sidebar.h"
#include "settings.h"

static const char *const s_dias[] = {"Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};
static lv_obj_t *s_clk, *s_date, *s_pill[3];

Theme TH;
/* principal: bg card btn press panel bar muted text */
static const uint32_t TP[4][8] = {
    {0x000000, 0x141414, 0x2A2A2A, 0x444444, 0x1A1A1A, 0x0B0B0B, 0xA8A8A8, 0xFFFFFF}, /* Preto */
    {0x07142B, 0x16345F, 0x25477F, 0x3A5FA0, 0x13305A, 0x0C2147, 0xAFC2E0, 0xFFFFFF}, /* Azul */
    {0xF4F4F4, 0xFFFFFF, 0xE3E3E3, 0xCCCCCC, 0xFFFFFF, 0xEDEDED, 0x555555, 0x111111}, /* Branco */
    {0xE9DEC7, 0xF4ECDA, 0xD8C9A8, 0xC5B38C, 0xF2E8D2, 0xDFD2B6, 0x6A5D44, 0x2A2316}, /* Bege */
};
static const uint32_t TA[6] = {0xFFFFFF, 0x22C55E, 0xEF4444, 0x000000, 0x2F80ED, 0xD8C8A8}; /* Branco Verde Vermelho Preto Azul Bege */
static void theme_apply(void)
{
    int m = hw_config_get(22), a = hw_config_get(23);
    const uint32_t *p = TP[m < 0 || m > 3 ? 0 : m];
    TH = (Theme){p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], TA[a < 0 || a > 5 ? 0 : a]};
}

/* texto legivel sobre o fundo dado: preto nos claros, branco nos escuros */
static uint32_t on_color(uint32_t c)
{
    return (((c >> 16) & 255) * 30 + ((c >> 8) & 255) * 59 + (c & 255) * 11) / 100 > 150 ? 0x111111 : 0xFFFFFF;
}

lv_obj_t *mk_btn(lv_obj_t *p, const char *txt, int32_t x, int32_t y, int32_t w, int32_t h,
                 uint32_t bg, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(on_color(bg)), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

lv_obj_t *mk_lbl(lv_obj_t *p, const char *txt, int32_t x, int32_t y, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

/* pilulas de status: Wi-Fi de casa (wlan1), cartao SD, celular no AP do CarPlay (wlan0 com cliente no ARP) */
static int st_wifi(void)
{
    char st[8] = "";
    FILE *f = fopen("/sys/class/net/wlan1/operstate", "r");
    if (f) fgets(st, sizeof st, f), fclose(f);
    return !strncmp(st, "up", 2);
}

static int st_sd(void) { return access("/mnt/extsd/video", F_OK) == 0; }

static int st_phone(void)
{
    char l[160];
    int on = 0;
    FILE *f = fopen("/proc/net/arp", "r");
    while (f && fgets(l, sizeof l, f))
        if (strstr(l, "wlan0") && strstr(l, "0x2")) on = 1; /* flags 0x2 = vizinho completo */
    if (f) fclose(f);
    return on;
}

static void pill_set(int i, int on)
{
    lv_obj_set_style_bg_color(s_pill[i], lv_color_hex(CARD), 0);
    lv_obj_set_style_bg_opa(s_pill[i], on ? 190 : 110, 0); /* translucidas: a foto aparece */
    lv_obj_set_style_text_color(s_pill[i], lv_color_hex(on ? ACC : MUTED), 0);
}

/* papel de parede: o do SD (1600x600, melhor) ou, sem SD por ~20 s, a copia leve do aparelho (1600x600, qualidade baixa).
 * O SD so monta depois que a home sobe: tenta de novo a cada segundo. */
static int s_wall, s_wall_n;
static void wall_try(void)
{
    if (s_wall) return;
    const char *src = !access(WALLPAPER, R_OK) ? "A:" WALLPAPER : (++s_wall_n > 20 && !access(WALL_DATA, R_OK)) ? "A:" WALL_DATA : NULL;
    if (!src) return;
    s_wall = 1;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *w = lv_image_create(scr);
    lv_image_set_src(w, src);
    lv_obj_set_pos(w, 0, 0);
    lv_obj_set_style_pad_all(w, 0, 0);
    lv_obj_set_style_border_width(w, 0, 0);
    lv_obj_remove_flag(w, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
#if WALL_DIM > 0
    lv_obj_t *sc = lv_obj_create(scr); /* mascara preta p/ o texto ler; o dono decide depois de ver a foto pura */
    lv_obj_set_pos(sc, 0, 0);
    lv_obj_set_size(sc, 1600, 600);
    lv_obj_set_style_bg_color(sc, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sc, WALL_DIM * 255 / 100, 0);
    lv_obj_set_style_radius(sc, 0, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(sc);
#endif
    lv_obj_move_background(w);
}

static void clock_cb(lv_timer_t *t)
{
    (void)t;
    wall_try();
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) return;
    lv_label_set_text_fmt(s_clk, "%02d:%02d", tm->tm_hour, tm->tm_min);
    lv_label_set_text_fmt(s_date, "%s, %02d/%02d/%04d", s_dias[tm->tm_wday],
                          tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    pill_set(0, st_wifi());
    pill_set(1, st_sd());
    pill_set(2, st_phone());
}

static void card_cb(lv_event_t *e)
{
    /* touchfix.so (dentro do cdr) le isto e toca no card igual da home do cdr: abre direto no CarPlay etc. */
    lv_label_set_text(lv_obj_get_child(lv_event_get_target_obj(e), 3), "Abrindo..."); /* subtitulo do card */
    lv_refr_now(NULL);
    hw_open_cdr_at(lv_event_get_user_data(e));
}

/* card de acesso: simbolo grande colorido + titulo + subtitulo */
static void mk_card(lv_obj_t *scr, int x, const char *sym, uint32_t cor, const char *tit, const char *sub, const char *xy)
{
    lv_obj_t *b = mk_btn(scr, "", x, 250, 460, 310, CARD, card_cb, (void *)xy);
    lv_obj_set_style_bg_opa(b, 205, 0); /* ~80%: a cor do tema aparece e o fundo ainda transparece */
    lv_obj_set_style_radius(b, 32, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(BTN), 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_label_set_text(l, "Abrir"); /* card_cb troca o filho 0 por "Abrindo..." */
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *i = lv_label_create(b);
    lv_label_set_text(i, sym);
    lv_obj_set_style_text_color(i, lv_color_hex(ACC), 0);
    (void)cor;
    lv_obj_set_style_transform_scale(i, 600, 0); /* 28 px -> ~65 px */
    lv_obj_align(i, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *t = lv_label_create(b);
    lv_label_set_text(t, tit);
    lv_obj_set_style_text_color(t, lv_color_hex(WHITE), 0);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -92);
    lv_obj_t *u = lv_label_create(b);
    lv_label_set_text(u, sub);
    lv_obj_set_style_text_color(u, lv_color_hex(MUTED), 0);
    lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -52);
}

void home_ui_create(lv_obj_t *scr)
{
    theme_apply();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, &font_acentos_28, 0);

    /* relogio e data */
    s_clk = mk_lbl(scr, "00:00", 130, 40, 0x000000); /* preto, pedido do dono */
    lv_obj_set_style_text_font(s_clk, &font_relogio_84, 0); /* fonte nativa: nitida (antes era 28 px ampliada 3x) */
    s_date = mk_lbl(scr, "", 130, 184, 0x000000);

    /* pilulas de status ao lado do relogio */
    static const char *const sy[3] = {LV_SYMBOL_WIFI " Wi-Fi", LV_SYMBOL_SD_CARD " SD", LV_SYMBOL_CALL " Celular"};
    for (int k = 0; k < 3; k++) {
        s_pill[k] = lv_obj_create(scr);
        lv_obj_set_size(s_pill[k], 170, 52);
        lv_obj_set_pos(s_pill[k], 520 + k * 186, 60);
        lv_obj_set_style_radius(s_pill[k], 26, 0);
        lv_obj_set_style_border_width(s_pill[k], 0, 0);
        lv_obj_set_style_pad_all(s_pill[k], 0, 0);
        lv_obj_remove_flag(s_pill[k], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *l = lv_label_create(s_pill[k]);
        lv_label_set_text(l, sy[k]);
        lv_obj_center(l);
    }

    /* cards de acesso */
    mk_card(scr, 130, LV_SYMBOL_PLAY, 0x2F80ED, "CarPlay", "iPhone", "1022 430"); /* x y do card igual na home do cdr */
    mk_card(scr, 614, LV_SYMBOL_AUDIO, 0x22C55E, "Android Auto", "Android", "1397 430");
    mk_card(scr, 1098, LV_SYMBOL_VIDEO, 0xF59E0B, "Registro de viagem", "Câmeras e gravações", "!647 430");

    sidebar_create(scr);
    /* pedido do menu do botao auxiliar (touchfix.so, sobre o CarPlay): "som" = abrir o volume */
    FILE *cmd = fopen("/tmp/ui_cmd", "r");
    if (cmd) {
        char b[8] = "";
        fgets(b, sizeof b, cmd), fclose(cmd), remove("/tmp/ui_cmd");
        if (!strncmp(b, "som", 3)) sidebar_open_som();
        else if (!strncmp(b, "conf", 4)) sidebar_open_config();
    }

    clock_cb(NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    saver_init();
}
