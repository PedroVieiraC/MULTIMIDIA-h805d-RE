#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/rtc.h>
#include "home_ui.h"
#include "settings.h"
#ifndef GIT_REV
#define GIT_REV "?"
#endif

enum { CHOICE, ACTION, DESTRUCT, INFO };
typedef struct { const char *nome; int icon; const char *val[6]; int nvals; int kind; } Opt;
enum { I_HORA = 13, I_AUDIO = 16, I_VER = 19 };

/* indice = id de hw_config_*; icon = set_itemN.png do cdr; valores = menu.cfg; rotulos = cdr */
static const Opt O[24] = {
    {"Resolução", 0, {"1080P", "2K", "4K"}, 3, CHOICE},
    {"Tempo do vídeo", 1, {"1 minuto", "3 minutos", "5 minutos"}, 3, CHOICE},
    {"Gravação", 2, {"Desligado", "Ligado"}, 2, CHOICE},
    {"Botão auxiliar", 14, {"Desligado", "Ligado"}, 2, CHOICE},
    {"Espelho dianteiro", 3, {"Desligado", "Ligado"}, 2, CHOICE},
    {"Espelhamento para trás", 3, {"Desligado", "Ligado"}, 2, CHOICE},
    {"Proteção de tela", 4, {"Desligado", "1 minuto", "2 minutos", "3 minutos"}, 4, CHOICE},
    {"Sensor de gravidade", 17, {"Desligado", "Baixo", "Médio", "Alto"}, 4, CHOICE},
    {"Posição de condução", 6, {"Condução à esquerda", "Condução à direita"}, 2, CHOICE},
    {"Modo de tela dividida CarPlay/Auto", 13, {"Modo de tela cheia", "Modo de meia tela"}, 2, CHOICE},
    {"Menu de fábrica", 12, {""}, 1, ACTION}, /* id 10: sem config; traz o cdr para a frente na home dele (volta: duplo power) */
    {"Calibração da linha de ré", 7, {""}, 1, ACTION},
    {"Configurações de idioma", 8, {"Português", "English"}, 2, CHOICE},
    {"Configurações de hora", 9, {""}, 1, ACTION},
    {"Limpar histórico de conexões", 15, {""}, 1, DESTRUCT},
    {"Efeitos sonoros de toque", 16, {"Desligado", "Ligado"}, 2, CHOICE},
    {"Saída de áudio", 16, {""}, 1, ACTION},
    {"Formatar cartão de memória", 10, {""}, 1, DESTRUCT},
    {"Restaurar configurações de fábrica", 11, {""}, 1, DESTRUCT},
    {"Versão do software", 12, {""}, 1, INFO},
    {"", 0, {""}, 1, INFO}, /* 20 = volume (nao e item do menu) */
    {"Apagar gravações antigas (SD cheio)", 10, {"Desligado", "Ligado"}, 2, CHOICE}, /* 21: nossa, tools/sd_clean.sh */
    {"Cor principal", 4, {"Preto", "Azul", "Branco", "Bege"}, 4, CHOICE}, /* 22: tema (fundo, cards, barra) */
    {"Cor secundária", 13, {"Branco", "Verde", "Vermelho", "Preto", "Azul", "Bege"}, 6, CHOICE}, /* 23: destaque (icones, botoes) */
};
/* ordem da grade do cdr (5 colunas) */
/* fora da grade (decisao do dono 16/11): 8 Posicao de conducao (sempre a esquerda), 12 Idioma (so portugues), 18 Restaurar fabrica (tira a nossa home) */
static const uint8_t ORD[20] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 14, 15, 16, 17, 21, 22, 23, 19, 10};

static lv_obj_t *s_set, *s_modal, *s_tl[5], *s_hmsg;
static int s_mid;

static void view_list(void);

static void back_cb(lv_event_t *e)
{
    (void)e;
    view_list();
}

/* icone do cdr lido do aparelho; arquivo ausente = sem icone (so o rotulo) */
static void mk_icon(lv_obj_t *p, int n, lv_align_t al, int32_t x, int32_t y)
{
    char path[48];
    snprintf(path, sizeof path, "/usr/share/images/set_item%d.png", n);
    if (access(path, R_OK)) return;
    snprintf(path, sizeof path, "A:/usr/share/images/set_item%d.png", n);
    lv_obj_t *im = lv_image_create(p);
    lv_image_set_src(im, path);
    lv_obj_align(im, al, x, y);
}

/* limpa e monta cabecalho (icone + nome) + rodape + Voltar sempre em (30,480) */
static void begin_view(int i)
{
    lv_obj_clean(s_set);
    mk_icon(s_set, O[i].icon, LV_ALIGN_TOP_LEFT, 40, 20);
    mk_lbl(s_set, O[i].nome, 160, 52, WHITE);
    mk_lbl(s_set, hw_config_mapped(i) ? "Salvo (vale ao abrir o CarPlay/DVR)" : "Ajustes ainda não são salvos", 1000, 565, MUTED);
    mk_btn(s_set, "Voltar", 30, 480, 300, 90, BTN, back_cb, NULL);
}

/* ---- modal em 2 passos (destrutivo) ---- */
static void modal_close(void)
{
    if (s_modal) lv_obj_delete_async(s_modal);
    s_modal = NULL;
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
}

static void ok_cb(lv_event_t *e)
{
    (void)e;
    hw_config_do(s_mid);
    modal_close();
}

static void modal_open(int id, int step);

static void next_cb(lv_event_t *e)
{
    (void)e;
    modal_open(s_mid, 1);
}

static void modal_open(int id, int step)
{
    modal_close();
    s_mid = id;
    s_modal = lv_obj_create(lv_layer_top()); /* cobre tudo, inclusive a barra */
    lv_obj_set_size(s_modal, 1600, 600);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, 0);
    lv_obj_set_style_radius(s_modal, 0, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_set_style_text_font(s_modal, &font_acentos_28, 0); /* layer_top nao herda da tela */
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *box = lv_obj_create(s_modal);
    lv_obj_set_size(box, 1000, 360);
    lv_obj_set_pos(box, 300, 120);
    lv_obj_set_style_bg_color(box, lv_color_hex(PANEL), 0);
    lv_obj_set_style_radius(box, 24, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    const char *txt = step ? "Confirma? Não tem volta."
                    : id == 17 ? "Formatar o cartão de memória? Apaga todos os vídeos."
                    : id == 14 ? "Limpar o histórico de conexões?"
                    : "Restaurar as configurações de fábrica?";
    lv_obj_t *l = mk_lbl(box, txt, 50, 70, WHITE);
    lv_obj_set_width(l, 900);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    /* passo 2 inverte as posicoes: Confirmar a esquerda, Cancelar a direita */
    if (step) {
        mk_btn(box, "Confirmar", 50, 200, 400, 120, 0xC0392B, ok_cb, NULL);
        mk_btn(box, "Cancelar", 550, 200, 400, 120, BTN, cancel_cb, NULL);
    } else {
        mk_btn(box, "Cancelar", 50, 200, 400, 120, BTN, cancel_cb, NULL);
        mk_btn(box, "Continuar", 550, 200, 400, 120, ACC, next_cb, NULL);
    }
}

/* ---- subtelas ---- */
static void choice_cb(lv_event_t *e)
{
    int u = (int)(intptr_t)lv_event_get_user_data(e);
    hw_config_set(u / 10, u % 10);
    if (u / 10 >= 22) hw_restart_ui(); /* tema: reabre a home com as cores novas */
    view_list();
}

static void view_choice(int i)
{
    int n = O[i].nvals, w = n == 6 ? 220 : n == 4 ? 310 : (n == 3 ? 400 : 480);
    int x0 = (1510 - (n * w + (n - 1) * 30)) / 2;
    begin_view(i);
    for (int v = 0; v < n; v++)
        mk_btn(s_set, O[i].val[v], x0 + v * (w + 30), 240, w, 120,
               v == hw_config_get(i) ? ACC : BTN, choice_cb, (void *)(intptr_t)(i * 10 + v));
}

static void view_info(int i)
{
    begin_view(i);
    if (i != I_VER) {
        mk_lbl(s_set, "Em breve", 160, 260, WHITE);
        return;
    }
    char v[64] = "?", l[128], b[64];
    FILE *f = fopen("/usr/share/configs/menu.cfg", "r"); /* so leitura */
    while (f && fgets(l, sizeof l, f))
        if (!strncmp(l, "version=", 8)) {
            char *q = l + 8;
            if (*q == '"') q++;
            q[strcspn(q, "\"\r\n")] = 0;
            snprintf(v, sizeof v, "%s", q);
            break;
        }
    if (f) fclose(f);
    snprintf(b, sizeof b, "Bordo/h805d-ui %s %s", __DATE__, GIT_REV);
    mk_lbl(s_set, b, 160, 260, WHITE);
    snprintf(b, sizeof b, "Sistema de fábrica: %s", v);
    mk_lbl(s_set, b, 160, 320, MUTED);
}

/* ---- hora: dia/mes/ano/hora/minuto com - e + ---- */
static const char *const HN[5] = {"Dia", "Mês", "Ano", "Hora", "Minuto"};
static const int HLO[5] = {1, 1, 2020, 0, 0}, HHI[5] = {31, 12, 2099, 23, 59};
static int s_tv[5];

static void hora_show(void)
{
    for (int k = 0; k < 5; k++) lv_label_set_text_fmt(s_tl[k], k == 2 ? "%04d" : "%02d", s_tv[k]);
}

static void hora_cb(lv_event_t *e)
{
    int u = (int)(intptr_t)lv_event_get_user_data(e), k = u >> 1;
    int v = s_tv[k] + ((u & 1) ? 1 : -1);
    s_tv[k] = v > HHI[k] ? HLO[k] : (v < HLO[k] ? HHI[k] : v);
    hora_show();
}

static void hora_save_cb(lv_event_t *e)
{
    (void)e;
    struct tm tm = {0};
    tm.tm_mday = s_tv[0]; tm.tm_mon = s_tv[1] - 1; tm.tm_year = s_tv[2] - 1900;
    tm.tm_hour = s_tv[3]; tm.tm_min = s_tv[4]; tm.tm_isdst = -1;
    time_t t = mktime(&tm); /* shortcut: 31/02 vira 03/03 (mktime normaliza), sem validar dia do mes */
    struct timeval tv = {t, 0};
    int ok = settimeofday(&tv, NULL) == 0;
    int fd = open("/dev/rtc0", O_RDWR); /* o cdr usa /dev/rtc0 */
    if (fd >= 0) {
        struct tm g;
        struct rtc_time r = {0};
        gmtime_r(&t, &g);
        r.tm_sec = g.tm_sec; r.tm_min = g.tm_min; r.tm_hour = g.tm_hour;
        r.tm_mday = g.tm_mday; r.tm_mon = g.tm_mon; r.tm_year = g.tm_year;
        ok = ioctl(fd, RTC_SET_TIME, &r) == 0 && ok;
        close(fd);
    } else ok = 0;
    lv_label_set_text(s_hmsg, ok ? "Hora salva" : "Não foi possível salvar a hora");
}

static void view_hora(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    begin_view(I_HORA);
    if (tm) {
        s_tv[0] = tm->tm_mday; s_tv[1] = tm->tm_mon + 1; s_tv[2] = tm->tm_year + 1900;
        s_tv[3] = tm->tm_hour; s_tv[4] = tm->tm_min;
    }
    for (int k = 0; k < 5; k++) {
        int x = 90 + k * 270;
        lv_obj_t *h = mk_lbl(s_set, HN[k], x, 130, MUTED);
        lv_obj_set_width(h, 250);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        mk_btn(s_set, LV_SYMBOL_PLUS, x, 170, 250, 90, BTN, hora_cb, (void *)(intptr_t)(k * 2 + 1));
        s_tl[k] = mk_lbl(s_set, "", x, 280, WHITE);
        lv_obj_set_width(s_tl[k], 250);
        lv_obj_set_style_text_align(s_tl[k], LV_TEXT_ALIGN_CENTER, 0);
        mk_btn(s_set, LV_SYMBOL_MINUS, x, 325, 250, 90, BTN, hora_cb, (void *)(intptr_t)(k * 2));
    }
    hora_show();
    s_hmsg = mk_lbl(s_set, "", 400, 505, MUTED);
    mk_btn(s_set, "Salvar", 1180, 480, 300, 90, ACC, hora_save_cb, NULL);
}

static lv_obj_t *s_ls;
static int32_t s_scroll; /* posicao da grade, restaurada ao voltar de um item */

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_scroll = lv_obj_get_scroll_y(s_ls);
    if (i == 10 || i == 11) hw_open_cdr_at("!50 492"); /* 11 Calibracao da re: abre o menu de fabrica (a grade dele tem o item) */ /* engrenagem da barra do cdr: cai no menu de fabrica */
    else if (i == I_AUDIO) hw_open_cdr_at("@1022 430"); /* card Saida de audio da pagina 2 da home do cdr (dono usa CarPlay/Auto; FM/AUX nossos removidos) */
    else if (i == I_HORA) view_hora();
    else if (O[i].kind == CHOICE) view_choice(i);
    else if (O[i].kind == DESTRUCT) modal_open(i, 0);
    else view_info(i); /* calibracao (em breve) e versao */
}

static void view_list(void)
{
    lv_obj_clean(s_set);
    lv_obj_t *ls = s_ls = lv_obj_create(s_set);
    lv_obj_set_pos(ls, 0, 0);
    lv_obj_set_size(ls, 1510, 600);
    lv_obj_set_style_bg_opa(ls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ls, 0, 0);
    lv_obj_set_style_pad_all(ls, 0, 0);
    lv_obj_set_style_radius(ls, 0, 0);
    lv_obj_remove_flag(ls, LV_OBJ_FLAG_SCROLL_ELASTIC); /* dono: sem snap e sem passar do limite */
    for (int n = 0; n < 20; n++) {
        int i = ORD[n];
        lv_obj_t *c = mk_btn(ls, O[i].nome, 22 + (n % 5) * 297, (n / 5) * 230, 297, 230, PRESS, row_cb,
                             (void *)(intptr_t)i);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_t *l = lv_obj_get_child(c, 0);
        lv_obj_set_width(l, 285);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 115);
        mk_icon(c, O[i].icon, LV_ALIGN_TOP_MID, 0, 12);
    }
    lv_obj_update_layout(ls);
    lv_obj_scroll_to_y(ls, s_scroll, LV_ANIM_OFF);
}

/* ---- protecao de tela: preto + relogio grande apos o tempo parado; 1o toque so fecha ---- */
static lv_obj_t *s_sv, *s_svc;

static void sv_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_delete_async(s_sv);
    s_sv = s_svc = NULL;
}

static void sv_tick(lv_timer_t *t)
{
    static const int T[4] = {0, 60, 120, 180}; /* s, do menu.cfg; sem FBIOBLANK/brilho (apagam de vez) */
    int sec = T[hw_config_get(6) & 3];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    (void)t;
    if (!s_sv) {
        if (!sec || lv_display_get_inactive_time(NULL) < (uint32_t)sec * 1000) return;
        s_sv = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_sv, 1600, 600);
        lv_obj_set_pos(s_sv, 0, 0);
        lv_obj_set_style_bg_color(s_sv, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_sv, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_sv, 0, 0);
        lv_obj_set_style_border_width(s_sv, 0, 0);
        lv_obj_remove_flag(s_sv, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_sv, sv_close_cb, LV_EVENT_CLICKED, NULL);
        s_svc = mk_lbl(s_sv, "00:00", 0, 0, WHITE);
        lv_obj_set_style_text_font(s_svc, &font_relogio_84, 0);
        lv_obj_center(s_svc);
    }
    if (tm && s_svc) lv_label_set_text_fmt(s_svc, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

void saver_init(void)
{
    lv_timer_create(sv_tick, 1000, NULL);
}

void settings_open(lv_obj_t *scr)
{
    if (!s_set) {
        s_set = lv_obj_create(scr);
        lv_obj_set_pos(s_set, 90, 0);
        lv_obj_set_size(s_set, 1510, 600);
        lv_obj_set_style_bg_color(s_set, lv_color_hex(BG), 0);
        lv_obj_set_style_radius(s_set, 0, 0);
        lv_obj_set_style_border_width(s_set, 0, 0);
        lv_obj_set_style_pad_all(s_set, 0, 0);
        lv_obj_remove_flag(s_set, LV_OBJ_FLAG_SCROLLABLE);
        s_scroll = 0; /* aberto pela barra: comeca do topo */
    }
    view_list();
}

void settings_close(void)
{
    modal_close();
    if (s_set) lv_obj_delete(s_set);
    s_set = NULL;
}
