/* h805d-ui: home própria em LVGL no /dev/fb0 (roda junto com o cdr vivo por trás) + toque Goodix + watchdog. */
#include "lvgl/lvgl.h"
#include <fcntl.h>
#include <poll.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32; /* tipos do header do kernel */
#include "sunxi_display2.h"
#include "home_ui.h"

static int ts_fd = -1, ts_x, ts_y, ts_pressed;
static volatile int quit;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Goodix expõe só ABS_MT_POSITION_X/Y (protocolo A, sem BTN_TOUCH):
 * frame (SYN_REPORT) com posição = tocando; frame vazio = soltou. */
static void ts_read(lv_indev_t *indev, lv_indev_data_t *d)
{
    (void)indev;
    static int frame_has_pos;
    static uint32_t last_pos;
    struct input_event e;
    while (read(ts_fd, &e, sizeof e) == sizeof e) {
        if (e.type == EV_ABS && (e.code == ABS_MT_POSITION_X || e.code == ABS_X)) ts_x = e.value, frame_has_pos = 1;
        else if (e.type == EV_ABS && (e.code == ABS_MT_POSITION_Y || e.code == ABS_Y)) ts_y = e.value, frame_has_pos = 1;
        else if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && e.value == -1) frame_has_pos = 0;
        else if (e.type == EV_SYN && e.code == SYN_REPORT) {
            if (frame_has_pos) {
                if (!ts_pressed) printf("%u touch down %d,%d\n", now_ms(), ts_x, ts_y);
                ts_pressed = 1;
                last_pos = now_ms();
            }
            frame_has_pos = 0;
        }
    }
    /* Goodix manda quadros vazios com o dedo parado (log: down/up a cada ~100 ms segurando).
     * Quadro vazio nao solta: so solta apos 250 ms sem posicao (maior buraco medido segurando: 160 ms). */
    if (ts_pressed && now_ms() - last_pos > 250) {
        printf("%u touch up %d,%d\n", now_ms(), ts_x, ts_y);
        ts_pressed = 0;
    }
    /* coords já em paisagem 0..1599/0..599 (medido nos 4 cantos); o EVIOCGABS do driver informa a faixa trocada — ignorar. */
    d->point.x = ts_x;
    d->point.y = ts_y;
    d->state = ts_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Mesma chamada do hwdisplay.c oficial (eyesee-mpp): args = {tela, &config, 1}. Versão 2 (CONFIG2):
 * a v1 não tem 'transform' e, ao reaplicar, perde a rotação da UI (painel 600x1600 retrato) = tela preta. */
static int layer_io(int fd, int cmd, struct disp_layer_config2 *c)
{
    unsigned long a[4] = {0, (unsigned long)c, 1, 0};
    return ioctl(fd, cmd, a);
}

/* Camadas de vídeo do cdr morto (ch0/ch1) apontam p/ buffers ION liberados -> DE falha = metade cinza.
 * Desliga só elas; a camada da UI (ch2) não é tocada. */
static void fix_layers(void)
{
    int fd = open("/dev/disp", O_RDWR);
    struct disp_layer_config2 c;
    for (unsigned ch = 0; ch < 3; ch++) {
        memset(&c, 0, sizeof c);
        c.channel = ch;
        int r = layer_io(fd, DISP_LAYER_GET_CONFIG2, &c);
        printf("ch%u get2=%d en=%d z=%d win=%d,%d %ux%u transform=%d\n", ch, r, c.enable, c.info.zorder,
               c.info.screen_win.x, c.info.screen_win.y, c.info.screen_win.width, c.info.screen_win.height, c.info.transform);
        if (r || ch == 2 || !c.enable) continue;
        c.channel = ch;
        c.enable = false;
        printf("ch%u off2=%d\n", ch, layer_io(fd, DISP_LAYER_SET_CONFIG2, &c));
    }
    close(fd);
    /* O SET desliga o gerenciador do display (tela preta, sysfs "sys" vazio). Um UNBLANK sozinho é
     * ignorado se o fb já se acha ligado (aconteceu: home piscava e apagava) -> ciclo completo. */
    fd = open("/dev/fb0", O_RDWR);
    ioctl(fd, FBIOBLANK, FB_BLANK_POWERDOWN);
    printf("unblank=%d\n", ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK));
    close(fd);
}

/* fb0 tem rotate=1: o kernel copia o fb (1600x600) girado p/ o buffer do DE (600x1600) a cada
 * FBIOPAN_DISPLAY, como o sunxifb faz no flush. O fbdev do LVGL não chama -> tela mostra buffer velho. */
static int pan_fd = -1;
static struct fb_var_screeninfo pan_vinfo;
static void pan_cb(lv_event_t *e) { (void)e; ioctl(pan_fd, FBIOPAN_DISPLAY, &pan_vinfo); }

/* ===== Hardware chamado pela UI (home_ui.c). Contrato em home_ui.h. ===== */

/* Tema novo: reabre a home (mesmo pid, o laco nem percebe) e volta direto para as Configuracoes. */
void hw_restart_ui(void)
{
    FILE *f = fopen("/tmp/ui_cmd", "w");
    if (f) fputs("conf", f), fclose(f);
    execl("/data/h805d-ui", "h805d-ui", (char *)NULL);
}

/* Abrir o sistema original: a home sai e o boot.sh sobe o cdr. */
void hw_open_cdr(void) { quit = 1; }

/* CarPlay na tela do cdr = camada de video de tela cheia fora do canal da UI (ch2), lida do sysfs do DE.
 * Medido: home do cdr = miniatura 256x296; camera = 600x1488 (barra lateral de fora) -> nao conta, o atalho sai dela.
 * shortcut: CarPlay em tela cheia inferido como altura >= 1500 (nao medido); meia tela nao e detectada. */
static int cdr_video_on(void)
{
    char l[512];
    int ch, x, y, w, h, on = 0;
    (void)x, (void)y;
    FILE *f = fopen("/sys/class/disp/disp/attr/sys", "r");
    if (!f) return 1;
    while (fgets(l, sizeof l, f)) {
        char *e = strstr(l, "enable ch["), *fr = strstr(l, "frame[");
        if (!e || !fr) continue;
        ch = atoi(e + 10); /* sem sscanf: +9 KB no binario */
        x = strtol(fr + 6, &fr, 10), y = strtol(fr + 1, &fr, 10), w = strtol(fr + 1, &fr, 10), h = strtol(fr + 1, &fr, 10);
        if (ch != 2 && w >= 500 && h >= 1500) on = 1;
    }
    fclose(f);
    return on;
}

void hw_open_cdr_at(const char *xy)
{
    int p2 = xy[0] == '@', leave = p2 || xy[0] == '!'; /* '!' = destino fora do CarPlay (camera, menu): sai dele e so volta pelo duplo power; '@' = idem, na pagina 2 da home do cdr */
    if (p2) close(open("/tmp/cdr_p2", O_CREAT | O_WRONLY, 0644));
    FILE *f = fopen("/tmp/cdr_open", "w");
    if (f) fputs(xy + leave, f), fclose(f);
    if (!cdr_video_on()) close(open("/tmp/cdr_force", O_CREAT | O_WRONLY, 0644)); /* touchfix: pode tocar as cegas */
    else if (leave) close(open("/tmp/cdr_float", O_CREAT | O_WRONLY, 0644));       /* touchfix: sai do CarPlay antes */
    if (leave) close(open("/tmp/cdr_stay", O_CREAT | O_WRONLY, 0644));             /* vigia: sem volta automatica */
    quit = 1;
}

/* ---- Brilho: ioctl 0x102 do display (sem /sys/class/backlight neste aparelho) ----
 * Igual ao cdr: /dev/disp aberto UMA vez e nunca fechado, args {0,V,0,0}, nunca o GET (0x103 apaga a tela; medido 12/11).
 * So age com /data/bright_ok (criado depois de um teste supervisionado); o nivel (10..100%) fica em /data/bright. */
static int disp_fd = -1;
static int disp_cmd(int cmd, unsigned long v)
{
    if (disp_fd < 0) disp_fd = open("/dev/disp", O_RDWR);
    unsigned long a[4] = {0, v, 0, 0};
    return ioctl(disp_fd, cmd, a);
}

/* 1 = o DE do display esta ligado (sysfs "sys" vazio = tela apagada) */
static int sys_on(void)
{
    FILE *f = fopen("/sys/class/disp/disp/attr/sys", "r");
    if (!f) return -1;
    int c = fgetc(f);
    fclose(f);
    return c != EOF;
}

#define BRIGHT_FILE "/data/bright"
#define BRIGHT_OK "/data/bright_ok"
static int bright_pct = -1;
static lv_timer_t *bright_save;
static int bright_raw(int pct) { return 50 + pct * 205 / 100; } /* piso 50/255: o "-" antigo levou a tela a ~0 */

int hw_brightness_get(void)
{
    if (bright_pct < 0) {
        char b[8] = "";
        FILE *f = fopen(BRIGHT_FILE, "r");
        if (f) fgets(b, sizeof b, f), fclose(f);
        bright_pct = atoi(b);
        if (bright_pct < 10 || bright_pct > 100) bright_pct = 100;
    }
    return bright_pct;
}

static void bright_save_cb(lv_timer_t *t)
{
    char b[8];
    lv_timer_pause(t);
    snprintf(b, sizeof b, "%d" "%c", bright_pct, 10);
    FILE *f = fopen(BRIGHT_FILE, "w"); /* 1 s depois do ultimo ajuste: arrastar o slider nao vira dezenas de escritas no flash */
    if (f) fputs(b, f), fclose(f);
}

/* Home: so grava o pedido (/tmp/ui_bright) e o nivel (/data/bright). Quem aplica e o vigia (--btn), que nunca sai:
 * o /dev/disp fechado logo depois de um SET apagou a tela (medido 16/11 com o modo --bright). */
void hw_brightness_set(int pct)
{
    bright_pct = pct < 10 ? 10 : pct > 100 ? 100 : pct;
    if (access(BRIGHT_OK, F_OK)) return; /* ainda nao liberado */
    char b[8];
    snprintf(b, sizeof b, "%d" "%c", bright_pct, 10);
    FILE *f = fopen("/tmp/ui_bright", "w");
    if (f) fputs(b, f), fclose(f);
    if (!bright_save) bright_save = lv_timer_create(bright_save_cb, 1000, NULL);
    lv_timer_reset(bright_save);
    lv_timer_resume(bright_save);
}

/* Vigia: aplica o pedido da home; depois do boot reaplica o nivel salvo por ~1 min (o cdr poe 255 ao subir) */
static void bright_apply(int pct)
{
    pct = pct < 10 ? 10 : pct > 100 ? 100 : pct;
    printf("brilho=%d%% raw=%d set=%d sys=%d%c", pct, bright_raw(pct), disp_cmd(DISP_LCD_SET_BRIGHTNESS, bright_raw(pct)), sys_on(), 10);
}

static void bright_tick(void)
{
    static int n;
    static uint32_t last;
    char b[8] = "";
    if (access(BRIGHT_OK, F_OK)) return;
    int f = open("/tmp/ui_bright", O_RDONLY);
    if (f >= 0) {
        int r = read(f, b, 7);
        close(f);
        unlink("/tmp/ui_bright");
        if (r > 0) bright_apply(atoi(b));
        return;
    }
    if (n < 20 && now_ms() - last > 3000) {
        last = now_ms(), n++;
        if (hw_brightness_get() < 100) bright_apply(hw_brightness_get());
    }
}

/* Volume = systemVolume do cdr (userConfig.bin 0x16, 0-100), aplicado como o cdr no ALSA "Soft Volume Master":
 * medido 75 -> 97/255 e 22 -> 28/255, ou seja 100% = ~129/255 (docs/launcher/USERCONFIG.md). */
#define VOL_CTL "Soft Volume Master"
#define CFG_VOL 20
int mixer_get_pct(const char *name);
int mixer_set_pct(const char *name, int pct);
static int vol_pct = -1;
static lv_timer_t *vol_save;
static void vol_save_cb(lv_timer_t *t)
{
    lv_timer_pause(t);
    hw_config_set(CFG_VOL, vol_pct); /* grava 1 s depois do ultimo ajuste: arrastar o slider nao vira dezenas de escritas no flash */
}
int hw_volume_get(void)
{
    if (vol_pct < 0) vol_pct = hw_config_get(CFG_VOL);
    return vol_pct;
}
void hw_volume_set(int pct)
{
    vol_pct = pct;
    printf("volume=%d (%d)\n", pct, mixer_set_pct(VOL_CTL, (pct * 129 + 127) / 255));
    if (!vol_save) vol_save = lv_timer_create(vol_save_cb, 1000, NULL);
    lv_timer_reset(vol_save);
    lv_timer_resume(vol_save);
}

/* ---- Saída de áudio: /dev/amux (amux.ko = FM QN8027 + pino do alto-falante).
 * cmds decifrados do amux_ioctl (só o byte baixo conta), arg = ponteiro p/ int:
 * 8 init FM, 9 freq (10 kHz: 8800 = 88,0 MHz), 11 mute FM, 12 transmissão FM, 13 alto-falante. */
enum { AMUX_FM_INIT = 8, AMUX_FM_FREQ = 9, AMUX_FM_MUTE = 11, AMUX_FM_TX = 12, AMUX_SPK = 13 };
static int amux(int cmd, int v)
{
    int fd = open("/dev/amux", O_RDWR);
    int r = ioctl(fd, cmd, &v);
    if (r) printf("amux cmd %d v %d falhou (fd %d)\n", cmd, v, fd);
    close(fd);
    return r;
}

static int fm_freq = 8800;
void hw_audio_out(int mode)
{
    if (mode == AUDIO_FM) {
        amux(AMUX_FM_INIT, 0);
        amux(AMUX_FM_FREQ, fm_freq);
        amux(AMUX_FM_MUTE, 0);
    }
    amux(AMUX_FM_TX, mode == AUDIO_FM);
    amux(AMUX_SPK, mode == AUDIO_SPK);
    printf("saida audio=%d fm=%d\n", mode, fm_freq);
}

void hw_fm_freq(int f)
{
    fm_freq = f < 7600 ? 7600 : f > 10800 ? 10800 : f;
    amux(AMUX_FM_FREQ, fm_freq);
}

/* Sinal de troca home <-> cdr: criado pelo vigia (--btn) no duplo clique do power; o boot.sh o consome. */
#define TOGGLE "/tmp/ui_toggle"
/* Batimento da home: recria o arquivo a cada ~0,5 s; o vigia o apaga e só alimenta o watchdog se ele reaparecer. */
#define ALIVE "/tmp/ui_alive"
static void alive_touch(void)
{
    close(open(ALIVE, O_CREAT | O_WRONLY, 0644));
}

/* Vigia alimenta se o cdr está na tela (home parada de propósito) ou se a home bateu há < 30 s.
 * Batimento = o vigia apaga o arquivo e a home recria: stat() dá EINVAL neste kernel 4.9 com o musl novo (ciclo de reboot em 13/11). */
static int feed_ok(uint32_t *seen)
{
    if (!access("/tmp/cdr_front", F_OK) || !unlink(ALIVE)) *seen = now_ms();
    return now_ms() - *seen < 30000;
}

/* Barra de som do touchfix (sobre o CarPlay): alvo 0-100 em /tmp/ui_vol. Aplica no ALSA na hora e grava o
 * userConfig.bin no ciclo seguinte sem toque (varios toques = uma escrita no flash). */
static void vol_tick(int *pend)
{
    char b[8] = {0};
    int f = open("/tmp/ui_vol", O_RDONLY);
    if (f >= 0) {
        read(f, b, 7), close(f), unlink("/tmp/ui_vol");
        int v = atoi(b);
        *pend = v < 0 ? 0 : v > 100 ? 100 : v;
        mixer_set_pct(VOL_CTL, (*pend * 129 + 127) / 255);
    } else if (*pend >= 0) hw_config_set(CFG_VOL, *pend), *pend = -1;
}

/* Faixa esquerda da tela (x=20, y=150 e 450) opaca? No CarPlay o fb do cdr e transparente ali (video por baixo);
 * na home do cdr e na nossa, a barra lateral e opaca. Dois pontos: o botao flutuante (~60 px) nao cobre os dois. */
static int fb_left_opaque(int fb)
{
    static const int ys[2] = {150, 450};
    for (int i = 0; i < 2; i++) {
        uint32_t px = 0;
        if (pread(fb, &px, 4, (ys[i] * 1600 + 20) * 4) != 4 || (px >> 24) < 200) return 0;
    }
    return 1;
}

/* Modo vigia: alimenta o watchdog (se a home bate) e fica no botão power (event1); dois apertos em < 600 ms = cria TOGGLE.
 * Também: se o cdr saiu do CarPlay (tela transparente) para a home dele (opaca por 1,5 s) = cria TOGGLE,
 * ou seja, o Home do botao flutuante do cdr volta para a nossa home. */
/* cdr rodando? (com touchfix.so ele fica vivo desenhando num buffer fantasma enquanto a home esta na frente) */
static int cdr_alive(void)
{
    FILE *f = popen("pidof cdr", "r");
    char b[16] = "";
    if (f) fgets(b, sizeof b, f), pclose(f);
    return b[0] >= '0' && b[0] <= '9';
}

static int btn_main(void)
{
    int fd = open("/dev/input/event1", O_RDONLY), fb = open("/dev/fb0", O_RDONLY);
    struct input_event e;
    uint32_t last = 0, opaque_since = 0, t0 = now_ms();
    int armed = 0; /* ja vimos o CarPlay (transparente) desde a ultima troca */
    int vpend = -1;
    int wd = -1; /* home e cdr rodam juntos: so o vigia alimenta o watchdog, condicionado ao batimento da home (feed_ok) */
    for (;;) {
        if (wd < 0 && (wd = open("/dev/watchdog", O_WRONLY)) >= 0) printf("vigia: watchdog aberto\n");
        if (wd >= 0 && feed_ok(&t0)) write(wd, "k", 1); /* shortcut: home travada com cdr na frente nao reinicia; sem batimento do cdr */
        vol_tick(&vpend);
        bright_tick();
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 200) > 0) { /* 200 ms: resposta da barra de som */
            if (read(fd, &e, sizeof e) != sizeof e) return 1;
            if (e.type != EV_KEY || e.code != KEY_POWER || e.value != 1) continue;
            uint32_t t = now_ms();
            if (last && t - last < 600) {
                close(open(TOGGLE, O_CREAT | O_WRONLY, 0644));
                printf("%u duplo power -> troca\n", t);
                last = 0;
                armed = 0;
            } else last = t;
            continue;
        }
        if (fb < 0) continue;
        if (access("/tmp/cdr_front", F_OK) || !access("/tmp/cdr_stay", F_OK)) { armed = 0, opaque_since = 0; continue; } /* so vale com o cdr na frente; cdr_stay = dono pediu o sistema original, so o duplo power volta */
        if (!fb_left_opaque(fb)) { /* so arma com o video do CarPlay: a tela preta do atalho tambem e transparente e disparava a volta */
            if (!armed) armed = cdr_video_on();
            opaque_since = 0;
        }
        else if (armed && !opaque_since) opaque_since = now_ms();
        else if (armed && now_ms() - opaque_since >= 1500) {
            close(open(TOGGLE, O_CREAT | O_WRONLY, 0644));
            printf("%u cdr saiu do CarPlay para a home dele -> troca\n", now_ms());
            armed = 0, opaque_since = 0;
        }
    }
}

/* Uso: h805d-ui [segundos] | h805d-ui --btn
 * home: 0/sem arg = roda até o botão ou o duplo power; sai com 0 e quem chamou sobe o cdr. */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--btn")) return btn_main();
    if (argc > 2 && !strcmp(argv[1], "--cfgtest")) return hw_config_test(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "--mixer")) { int mixer_list(void); return mixer_list(); } /* diagnostico: lista controles ALSA */
    if (argc > 2 && !strcmp(argv[1], "--bright")) { /* diagnostico: valor bruto 0..255, um fd, sem GET */
        printf("sys antes=%d%c", sys_on(), 10);
        printf("set=%d%c", disp_cmd(DISP_LCD_SET_BRIGHTNESS, atoi(argv[2])), 10);
        usleep(1500000);
        printf("sys depois=%d%c", sys_on(), 10);
        return 0;
    }
    int secs = argc > 1 ? atoi(argv[1]) : 0;
    alive_touch(); /* ja no inicio: o batimento antigo pode estar velho (cdr estava na frente) */
    unlink("/tmp/cdr_stay");
    unlink(TOGGLE); /* duplo power antigo não pode fechar a home recém-aberta */

    /* cdr segura o /dev/watchdog; com ele morto, nós alimentamos (abrir em retry abaixo). Se o app travar, o watchdog reinicia o aparelho. */
    int wd = -1;

    lv_init();
    lv_tick_set_cb(now_ms);
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
    /* DEPOIS do LVGL abrir o fb: antes dele, o unblank do LVGL mostra o buffer velho do cdr (testado 2026-11-12). Custo: ~1 s de cinza. */
    if (!cdr_alive()) fix_layers(); /* cdr vivo (por tras, com touchfix.so): as camadas de video sao dele, nao mexer */
    pan_fd = open("/dev/fb0", O_RDWR);
    ioctl(pan_fd, FBIOGET_VSCREENINFO, &pan_vinfo);
    printf("pan=%d\n", ioctl(pan_fd, FBIOPAN_DISPLAY, &pan_vinfo));
    lv_display_add_event_cb(disp, pan_cb, LV_EVENT_REFR_READY, NULL);

    ts_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    lv_indev_t *ts = lv_indev_create();
    lv_indev_set_type(ts, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ts, ts_read);
    lv_indev_set_scroll_limit(ts, 5); /* arrasto comeca com 5 px (padrao 10): rolagem responde antes */
    lv_indev_set_scroll_throw(ts, 30); /* inercia curta (padrao 10): para logo ao soltar */

    home_ui_create(lv_screen_active());

    uint32_t end = now_ms() + secs * 1000u, last_feed = 0;
    while (!quit && (!secs || (int32_t)(end - now_ms()) > 0)) {
        if (now_ms() - last_feed > 500) {
            if (wd < 0 && (wd = open("/dev/watchdog", O_WRONLY)) >= 0) printf("watchdog aberto fd=%d\n", wd); /* cdr pode demorar a soltar */
            if (wd >= 0) write(wd, "k", 1);
            alive_touch();
            if (!unlink(TOGGLE)) quit = 2; /* duplo power: vai pro cdr */
            last_feed = now_ms();
        }
        usleep(lv_timer_handler() * 1000);
    }
    printf("saindo (%s)\n", quit == 2 ? "duplo power" : quit ? "botao" : "timeout");
    /* sem magic close 'V' de propósito: o watchdog segue armado até o cdr reabrir. */
    return 0;
}
