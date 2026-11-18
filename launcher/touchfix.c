/* touchfix.so: LD_PRELOAD no cdr. Tres funcoes:
 * 1. Touch: o Goodix manda quadros sem posicao no meio de um toque; o cdr repassava isso ao iPhone como
 *    soltar/apertar e o arrasto no CarPlay (Waze) ficava picado. Quadro sem posicao durante um toque fica
 *    retido: se vier posicao em ate HOLD_MS e descartado; senao e entregue (soltura de verdade).
 * 2. Tela fantasma: o mmap do /dev/fb0 do cdr e trocado (mesmo endereco, MAP_FIXED) entre o fb real e um
 *    buffer em /tmp. Sem /tmp/cdr_front o cdr desenha no fantasma e nao recebe toques: a nossa home fica na
 *    frente e o cdr segue vivo (CarPlay conectado, DVR gravando). Com o arquivo, volta ao fb real na hora.
 *    /tmp/cdr_open ("x y") + /tmp/cdr_force (a home garante que o cdr nao esta no CarPlay) = atalho: na
 *    frente, toca no Home do cdr e depois em x y da home dele. Sem cdr_force (CarPlay na tela) so mostra.
 * 3. Botao auxiliar nosso (no lugar do flutuante de fabrica, desligado em /data/aux_off): desenhado sobre o
 *    cdr na frente; tocar abre um menu em cruz: Home (/tmp/ui_toggle = volta a nossa home), Som (barra de
 *    volume no topo, sobre o CarPlay: - e + gravam o alvo em /tmp/ui_vol e o vigia aplica; some em 3 s),
 *    Camera e Brilho (cinza: ainda sem acao).
 * Build: sh build_touchfix.sh. Sem libc no link (aparelho: musl 1.1.24; Alpine: 1.2): so simbolos de ABI
 * igual nas duas; relogio pelo syscall 263 (o clock_gettime da 1.2 e time64). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HOLD_MS 250
#define EV_SZ 16
#define MAXF 64 /* eventos por quadro */
#define W 1600  /* fb0: 1600x600 BGRA */
#define BX 1360 /* botao auxiliar: centro e raio (85% da largura) */
#define BY 300
#define BR 38

typedef struct { int32_t s, us; uint16_t type, code; int32_t value; } ev_t; /* input_event do kernel 32 bits */

static int tfd = -1, contact, btn_gest; /* btn_gest: 1 = gesto no botao, 2 = com o menu aberto, 3 = na barra de som */
static volatile int vol_want, vol_shown, vol_dirty;
static int vol;
static uint32_t vol_until;
static void vol_pick(int x);
#define VX0 500 /* barra de som: topo da tela, longe do botao */
#define VY0 16
#define VW 600
#define VH 84
static int in_vol(int x, int y) { return x >= VX0 && x < VX0 + VW && y < VY0 + VH + 12; }
static volatile int menu_want, menu_shown, btn_drawn;
static void menu_show(int on);
static void vol_show(int on);
static uint32_t last_pos;
static ev_t cur[MAXF], held[MAXF], out[4 * MAXF];
static int ncur, nheld, has_pos, nout, oout, last_x, last_y;
static ssize_t (*real_read)(int, void *, size_t);
static int (*real_open)(const char *, int, ...);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static long (*sys)(long, ...);

static int fbfd = -1, shfd = -1;
static uint8_t g2d[1024]; /* fds do /dev/g2d (o cdr abre mais de um) */
static uint8_t *fbaddr, *realv, *shv;
static size_t fblen;
static volatile int front = 1;

static void dbg(const char *m, uint32_t v)
{
    char b[64];
    int i = 0;
    while (*m && i < 48) b[i++] = *m++;
    char t[12];
    int k = 0;
    do t[k++] = "0123456789"[v % 10]; while ((v /= 10) && k < 11);
    while (k) b[i++] = t[--k];
    b[i++] = 10;
    static int lfd = -1;
    if (lfd < 0 && real_open) lfd = real_open("/tmp/touchfix.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sys && lfd >= 0) sys(4, lfd, b, i);
}

static uint32_t now_ms(void)
{
    long ts[2];
    sys(263, 1, ts); /* __NR_clock_gettime, CLOCK_MONOTONIC */
    return (uint32_t)ts[0] * 1000u + (uint32_t)ts[1] / 1000000u;
}

static int exists(const char *p) { return sys(33, p, 0) == 0; } /* access(F_OK) */
static void touch_file(const char *p)
{
    int f = real_open(p, O_WRONLY | O_CREAT, 0644);
    if (f >= 0) sys(6, f);
}

static void emit(const ev_t *e, int n)
{
    if (nout + n > 4 * MAXF) n = 4 * MAXF - nout;
    memcpy(out + nout, e, n * sizeof *e);
    nout += n;
}

static int in_btn(int x, int y) { return (x - BX) * (x - BX) + (y - BY) * (y - BY) <= (BR + 12) * (BR + 12); }

static void frame_done(void)
{
    if (!front) { /* home na frente: o cdr nao recebe toque nenhum */
        ncur = has_pos = nheld = contact = btn_gest = 0;
        return;
    }
    if (has_pos) {
        int repress = 0; /* aperto novo antes dos 250 ms da soltura anterior (shortcut: so vale se a soltura retida traz PRESSURE=0 ou BTN_TOUCH=0) */
        for (int i = 0; i < nheld; i++)
            if (held[i].type == 3 && held[i].code == 0x3a && held[i].value == 0) repress = 1;
            else if (held[i].type == 1 && held[i].code == 0x14a && held[i].value == 0) repress = 1;
        if (!contact || (btn_gest == 3 && repress)) {
            btn_gest = !fbaddr ? 0 : menu_want ? 2 : (vol_shown && in_vol(last_x, last_y)) ? 3 : (btn_drawn && in_btn(last_x, last_y)); /* gesto nosso */
            if (btn_gest == 3) vol_pick(last_x); /* no apertar: nao espera os 250 ms da soltura */
        }
        nheld = 0; /* quadro vazio retido seguido de posicao: era falso, some */
        contact = 1;
        last_pos = now_ms();
        if (!btn_gest) emit(cur, ncur);
    } else if (contact) {
        if (nheld + ncur <= MAXF) { /* talvez seja soltura: segura (a soltura do cdr e PRESSURE=0 + quadro vazio) */
            memcpy(held + nheld, cur, ncur * sizeof *cur);
            nheld += ncur;
        }
    } else emit(cur, ncur);
    ncur = has_pos = 0;
}

/* Menu em cruz centrado no botao (cabe na tela: BX+115+IR < W): 0 Home, 1 Som, 2 Camera, 3 Brilho. */
#define MX BX
#define MY BY
#define IR 46
static const int IX[4] = {MX, MX - 115, MX + 115, MX}, IY[4] = {MY - 115, MY, MY, MY + 115};
static void menu_pick(int x, int y)
{
    int it = -1;
    for (int i = 0; i < 4; i++)
        if ((x - IX[i]) * (x - IX[i]) + (y - IY[i]) * (y - IY[i]) <= (IR + 10) * (IR + 10)) it = i;
    if (it == 1) { /* som: barra sobre o CarPlay; nivel inicial = systemVolume do cdr (userConfig.bin 0x16) */
        uint8_t b[0x17];
        int f = real_open("/data/userConfig.bin", O_RDONLY, 0);
        vol = 50;
        if (f >= 0) {
            if (real_read(f, b, sizeof b) == (ssize_t)sizeof b && b[0x16] <= 100) vol = b[0x16];
            sys(6, f);
        }
        vol_until = now_ms() + 3000, vol_want = 1;
    }
    if (it == 0) touch_file("/tmp/ui_toggle"), dbg("menu auxiliar item ", it);
    menu_want = 0; /* camera/brilho (sem acao ainda) ou fora: so fecha */
}

static void vol_pick(int x)
{
    int rx = x - VX0, v = vol;
    if (rx < 140) v -= 5; /* - */
    else if (rx >= VW - 140) v += 5; /* + */
    else v = (rx - 140) * 100 / (VW - 280); /* toque no trilho: nivel naquele ponto */
    vol = v < 0 ? 0 : v > 100 ? 100 : v;
    char b[4] = {'0' + vol / 100, '0' + vol / 10 % 10, '0' + vol % 10, 10};
    int f = real_open("/tmp/ui_vol", O_WRONLY | O_CREAT | O_TRUNC, 0644); /* o vigia (h805d-ui --btn) aplica no ALSA e grava */
    if (f >= 0) sys(4, f, b, 4), sys(6, f);
    vol_until = now_ms() + 3000, vol_dirty = 1;
}

static void flush_held(void)
{
    if (nheld && now_ms() - last_pos > HOLD_MS) {
        if (btn_gest == 1) {
            if (in_btn(last_x, last_y)) menu_want = 1;
        } else if (btn_gest == 2) menu_pick(last_x, last_y);
        else emit(held, nheld);
        nheld = contact = btn_gest = 0;
    }
}

/* Toque injetado (atalho dos cards): aperta, PRESS_MS depois solta. Entregue mesmo com a home na frente. */
#define PRESS_MS 150
static volatile int inj_x = -1;
static int inj_y, inj_step;
static uint32_t inj_at;

static void inj_load(void)
{
    char b[24] = {0};
    int f = real_open("/tmp/cdr_open", O_RDONLY, 0);
    if (f < 0) return;
    int n = real_read(f, b, sizeof b - 1);
    sys(6, f);
    sys(10, "/tmp/cdr_open"); /* unlink: so vale uma vez */
    int x = 0, y = 0, i = 0;
    while (i < n && b[i] >= '0' && b[i] <= '9') x = x * 10 + b[i++] - '0';
    while (i < n && b[i] == ' ') i++;
    while (i < n && b[i] >= '0' && b[i] <= '9') y = y * 10 + b[i++] - '0';
    if (x > 0 && y > 0) inj_y = y, inj_step = 0, inj_at = now_ms(), inj_x = x, dbg("atalho x=", x);
}

static void inj_tick(void)
{
    if (inj_x < 0 || contact || nheld || (int32_t)(now_ms() - inj_at) < 0) return;
    if (!inj_step) {
        ev_t f[5] = {{0, 0, 3, 0x35, inj_x}, {0, 0, 3, 0x36, inj_y}, {0, 0, 3, 0x3a, 50}, {0, 0, 0, 2, 0}, {0, 0, 0, 0, 0}};
        for (int k = 0; k < 3; k++) emit(f, 5);
        inj_step = 1;
        inj_at = now_ms() + PRESS_MS;
    } else {
        ev_t r[5] = {{0, 0, 3, 0x3a, 0}, {0, 0, 0, 2, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 2, 0}, {0, 0, 0, 0, 0}};
        emit(r, 5);
        inj_step = 0;
        inj_x = -1;
    }
}

/* ---- tela fantasma ---- */
static void pan(void)
{
    char var[256]; /* fb_var_screeninfo (160 B) */
    if (sys(54, fbfd, 0x4600, var) == 0) sys(54, fbfd, 0x4606, var); /* FBIOGET_VSCREENINFO, FBIOPAN_DISPLAY */
}

static uint32_t *px(uint8_t *b, int x, int y) { return (uint32_t *)(b + (y * W + x) * 4); }

static void go_front(void)
{
    /* cdr entrou no CarPlay atras (G2D descartado): sobram pedacos da home dele no fantasma. CarPlay = faixa
     * esquerda transparente; ai a tela vai limpa. shortcut: apaga tambem UI legitima que o cdr desenhou atras
     * (ex.: flutuante de fabrica, tela da camera); se fizer falta, limpar so as areas conhecidas. */
    if ((*px(shv, 20, 150) >> 24) < 200 && (*px(shv, 20, 450) >> 24) < 200) memset(realv, 0, fblen);
    else memcpy(realv, shv, fblen);
    real_mmap(fbaddr, fblen, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fbfd, 0);
    front = 1;
    pan();
    dbg("cdr na frente ", 0);
}

static void go_back(void)
{
    front = 0; /* primeiro: para de entregar toques */
    if (menu_shown) menu_show(0);
    if (vol_shown) vol_show(0);
    menu_want = vol_want = 0;
    if (btn_drawn) { /* nao leva o botao para a imagem guardada do cdr */
        for (int y = BY - BR; y <= BY + BR; y++) memset(px(realv, BX - BR, y), 0, (2 * BR + 1) * 4);
        btn_drawn = 0;
    }
    memcpy(shv, realv, fblen);
    real_mmap(fbaddr, fblen, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shfd, 0);
    dbg("cdr atras ", 0);
}

#define C_FILL 0x60202830u /* botao discreto sobre o mapa */
#define C_RING 0xA0FFFFFFu
#define C_MFILL 0xD0202830u /* menu: bem visivel */
#define C_MRING 0xFFFFFFFFu
#define C_OFF 0xFF6A6A6Au
static int glyph(int k, int hx, int hy) /* desenho simples de cada icone, em torno do centro */
{
    int ax = hx < 0 ? -hx : hx, ay = hy < 0 ? -hy : hy;
    switch (k) {
    case 0: return (hy >= -14 && hy <= -2 && ax <= hy + 16) || (hy > -2 && hy <= 14 && ax <= 10); /* casa */
    case 1: return (hx >= -16 && hx <= -8 && ay <= 6) || (hx > -8 && hx <= 4 && ay <= 6 + (hx + 8)) || (hx >= 9 && hx <= 12 && ay <= 10); /* alto-falante */
    case 2: return (ax <= 18 && ay <= 12 && (hx * hx + hy * hy) > 36) || (hy >= -16 && hy < -12 && ax <= 7); /* camera */
    default: return hx * hx + hy * hy <= 81 || (ax <= 2 && ay <= 18) || (ay <= 2 && ax <= 18); /* sol */
    }
}
static void disc(int cx, int cy, int r, int k, uint32_t fg, uint32_t fill)
{
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d > r * r) continue;
            *px(realv, x, y) = (d >= (r - 3) * (r - 3) || glyph(k, x - cx, y - cy)) ? fg : fill;
        }
}
static void draw_button(void)
{
    if (exists("/data/aux_off")) { /* desligado no menu de configuracoes da home */
        if (btn_drawn) {
            for (int y = BY - BR; y <= BY + BR; y++) memset(px(realv, BX - BR, y), 0, (2 * BR + 1) * 4);
            btn_drawn = 0, pan();
        }
        return;
    }
    if (btn_drawn && *px(realv, BX, BY - 8) == C_RING && *px(realv, BX + 24, BY) == C_FILL) return; /* ainda la */
    disc(BX, BY, BR, 0, C_RING, C_FILL);
    btn_drawn = 1;
    pan();
}
#define SX0 (MX - 115 - IR)
#define SY0 (MY - 115 - IR)
#define SW (2 * (115 + IR) + 1)
static uint32_t under[SW * SW]; /* o que havia sob o menu */
static void menu_show(int on)
{
    for (int y = 0; y < SW; y++)
        if (on) memcpy(under + y * SW, px(realv, SX0, SY0 + y), SW * 4);
        else memcpy(px(realv, SX0, SY0 + y), under + y * SW, SW * 4);
    if (on)
        for (int i = 0; i < 4; i++) disc(IX[i], IY[i], IR, i, i >= 2 ? C_OFF : C_MRING, C_MFILL);
    menu_shown = on;
    pan();
}

static uint32_t vunder[VW * VH]; /* o que havia sob a barra de som */
static void vol_show(int on)
{
    int cy = VH / 2, fillx = 140 + (VW - 280) * vol / 100;
    for (int y = 0; y < VH; y++) {
        uint32_t *row = px(realv, VX0, VY0 + y);
        if (on && !vol_shown) memcpy(vunder + y * VW, row, VW * 4);
        if (!on) { memcpy(row, vunder + y * VW, VW * 4); continue; }
        int ay = y < cy ? cy - y : y - cy;
        for (int x = 0; x < VW; x++) {
            int am = x < 60 ? 60 - x : x - 60, ap = x < VW - 60 ? VW - 60 - x : x - (VW - 60);
            int sign = (ay <= 3 && (am <= 18 || ap <= 18)) || (ap <= 3 && ay <= 18); /* - a esquerda, + a direita */
            int bar = x >= 140 && x < VW - 140 && ay <= 8;
            row[x] = sign || (bar && x < fillx) ? C_MRING : bar ? C_OFF : C_MFILL;
        }
    }
    vol_shown = on, vol_dirty = 0;
    pan();
}

/* Leva o cdr (na frente) para a pagina 1 da home dele, de qualquer tela. Medido em 13/11 por toque simulado:
 * - telas com a barra das subtelas (camera, pagina do CarPlay): Home em (56,205); (56,160) acerta e nao faz nada na home;
 * - home e menu de fabrica: Home em (50,105); no menu volta a home, NA HOME TROCA DE PAGINA (1 <-> 2);
 * - indicador de pagina da home: bolinhas em (1017,586) e (1038,586), a cheia (clara) e a pagina atual.
 * shortcut: nao cobre subtelas do menu de fabrica (ex.: Saida de audio em tela cheia). */
static void tap(int x, int y)
{
    inj_y = y, inj_step = 0, inj_at = now_ms(), inj_x = x;
    for (int t = 0; t < 100 && inj_x >= 0; t++) poll(0, 0, 20);
    poll(0, 0, 450); /* o cdr troca de tela */
}
static int bright(int x, int y)
{
    uint32_t v = *px(realv, x, y);
    return (v & 255) + (v >> 8 & 255) + (v >> 16 & 255) > 500;
}
static void cdr_home(void)
{
    tap(56, 160);
    if (!bright(1017, 586) && !bright(1038, 586)) tap(50, 125); /* menu, ou home ainda nao desenhada */
    if (bright(1038, 586)) tap(50, 125); /* pagina 2 -> 1 */
}

static void *mgr(void *u)
{
    (void)u;
    for (;;) {
        poll(0, 0, 20);
        int want = exists("/tmp/cdr_front");
        if (want && !front) {
            if (exists("/tmp/cdr_open")) {
                int fl = exists("/tmp/cdr_float"); /* CarPlay na tela e o atalho precisa sair dele (camera, menu) */
                if (fl || exists("/tmp/cdr_force")) { /* cdr_force: a home garante que o cdr nao esta no CarPlay */
                    go_front();
                    if (fl) { /* flutuante de fabrica (invisivel aqui, mas ativo) em (1446,300); Home do menu dele em
                               * (800,200): o cdr vai para a home dele sem derrubar o CarPlay (medido em 13/11).
                               * shortcut: se o dono arrastar o flutuante no fluxo de fabrica, a posicao muda. */
                        tap(1446, 300);
                        tap(800, 200);
                        poll(0, 0, 600);
                    }
                    cdr_home();
                    if (!sys(10, "/tmp/cdr_p2")) tap(50, 125); /* destino na pagina 2 (unlink ok = pedido) */
                    inj_load();
                    for (int t = 0; t < 100 && inj_x >= 0; t++) poll(0, 0, 20);
                } else sys(10, "/tmp/cdr_open"); /* CarPlay na tela: so mostra */
                sys(10, "/tmp/cdr_force");
                sys(10, "/tmp/cdr_float");
            }
            if (!front) go_front();
        } else if (!want && front) go_back();
        if (front && menu_want != menu_shown) menu_show(menu_want);
        if (front && vol_want && (int32_t)(now_ms() - vol_until) > 0) vol_want = 0; /* 3 s sem toque: some */
        if (front && !menu_shown && (vol_want != vol_shown || (vol_shown && vol_dirty))) vol_show(vol_want);
        if (front && !menu_shown) draw_button();
    }
    return 0;
}

void *mmap(void *a, size_t len, int prot, int flags, int fd, off_t off)
{
    if (!real_mmap) real_mmap = dlsym(RTLD_NEXT, "mmap");
    void *p = real_mmap(a, len, prot, flags, fd, off);
    if (fd < 0 || fd != fbfd || p == MAP_FAILED || fbaddr || off || len < (size_t)W * 600 * 4) return p;
    realv = real_mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    shfd = real_open("/tmp/cdr_shadow", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (realv == MAP_FAILED || shfd < 0 || sys(93, shfd, (long)len)) { dbg("fantasma falhou ", 0); return p; } /* ftruncate */
    shv = real_mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, shfd, 0);
    if (shv == MAP_FAILED) { dbg("fantasma falhou ", 1); return p; }
    fbaddr = p, fblen = len;
    if (!exists("/tmp/cdr_front")) { /* ja nasce atras: a limpeza inicial do cdr nao apaga a nossa home */
        front = 0;
        real_mmap(fbaddr, fblen, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shfd, 0);
    }
    pthread_t th;
    pthread_create(&th, 0, mgr, 0);
    dbg("fantasma pronto, bytes=", len);
    return p;
}

/* Atras, o cdr nao reconfigura nem apaga a tela (FBIOPUT_VSCREENINFO, FBIOBLANK) nem usa o G2D. */
int ioctl(int fd, int req, ...)
{
    static int (*f)(int, int, void *);
    if (!f) f = dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd == fbfd && !front && (req == 0x4601 || req == 0x4611)) return 0;
    if (fd >= 0 && fd < 1024 && g2d[fd] && !front) return 0; /* G2D escreve no endereco fisico do fb real: atras, descarta */
    return f(fd, req, arg);
}

int open(const char *path, int flags, ...)
{
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    if (!sys) sys = dlsym(RTLD_NEXT, "syscall");
    va_list ap;
    va_start(ap, flags);
    int mode = va_arg(ap, int);
    va_end(ap);
    int fd = real_open(path, flags, mode);
    if (fd >= 0 && path && !strcmp(path, "/dev/input/event0")) tfd = fd, ncur = nheld = nout = oout = contact = 0;
    if (fd >= 0 && path && !strcmp(path, "/dev/fb0") && fbfd < 0) fbfd = fd, front = exists("/tmp/cdr_front"); /* antes de qualquer ioctl */
    if (fd >= 0 && fd < 1024) g2d[fd] = path && !strcmp(path, "/dev/g2d"); /* fd reaproveitado deixa de ser g2d */
    return fd;
}
int open64(const char *path, int flags, ...) __attribute__((alias("open"))); /* busybox (teste) usa open64; o cdr usa open */

ssize_t read(int fd, void *buf, size_t n)
{
    if (!real_read) real_read = dlsym(RTLD_NEXT, "read"), sys = dlsym(RTLD_NEXT, "syscall");
    if (fd != tfd || n < EV_SZ) return real_read(fd, buf, n);
    for (;;) {
        if (oout < nout) { /* entrega o que ja foi filtrado */
            int k = nout - oout;
            if ((size_t)k > n / EV_SZ) k = n / EV_SZ;
            memcpy(buf, out + oout, k * EV_SZ);
            if ((oout += k) == nout) oout = nout = 0;
            return k * EV_SZ;
        }
        flush_held();
        inj_tick();
        if (nout) continue;
        int nb = fcntl(fd, F_GETFL) & O_NONBLOCK;
        if (!nb && nheld) { /* leitura bloqueante com soltura retida: espera so ate o prazo */
            struct pollfd p = {fd, POLLIN, 0};
            int wait = HOLD_MS - (int)(now_ms() - last_pos);
            if (poll(&p, 1, wait > 0 ? wait : 0) == 0) continue;
        }
        ev_t in[MAXF];
        ssize_t r = real_read(fd, in, sizeof in);
        if (r <= 0) return r; /* EAGAIN no nao bloqueante: cdr tenta de novo no proximo ciclo */
        for (int i = 0; i < r / EV_SZ; i++) {
            if (ncur < MAXF) cur[ncur++] = in[i];
            if (in[i].type == 3 && in[i].code == 0x35) last_x = in[i].value, has_pos = 1;
            if (in[i].type == 3 && in[i].code == 0x36) last_y = in[i].value, has_pos = 1;
            if (in[i].type == 0 && in[i].code == 0) frame_done(); /* SYN_REPORT */
        }
    }
}
