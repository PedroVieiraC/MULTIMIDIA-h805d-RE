#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "home_ui.h"

#define CFG_IN  "/data/userConfig.bin"
#define CFG_OUT CFG_IN /* liberado pelo dono em 12/11; backup em /data/userConfig.bak */
#define CFG_LEN 308
#define N 24 /* 20 opcoes do menu + 20 = volume + 21 = limpeza do SD + 22/23 = cor principal/secundaria (nossas) */

/* padroes = menu.cfg; nao mapeadas ficam so em RAM */
static int v[N] = {0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 75, 1, 0, 0};
/* offset no bin por opcao (0 = nao mapeada); codificacao do cdr = indice do valor (doc USERCONFIG.md) */
static const uint8_t OFF[N] = {0x08, 0x0A, 0x0D, 0, 0x0E, 0x0F, 0x11, 0x1E, 0x12, 0x32,
                                0, 0, 0, 0, 0, 0x39, 0, 0, 0, 0, 0x16, 0, 0, 0};
static uint8_t buf[CFG_LEN];
static int loaded, valid;

static uint32_t crc(const uint8_t *d)
{
    uint32_t c = 0xFFFFFFFF;
    for (int i = 4; i < CFG_LEN; i++) {
        c ^= (uint32_t)d[i] << 24;
        for (int k = 0; k < 8; k++) c = c & 0x80000000 ? (c << 1) ^ 0x04C11DB7 : c << 1;
    }
    return c;
}

static uint32_t stored(void) { return buf[0] | buf[1] << 8 | buf[2] << 16 | (uint32_t)buf[3] << 24; }

/* 0 = ok e valores copiados para v[]; senao motivo no printf */
static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("userConfig: %s ausente, usando RAM\n", path); return -1; }
    size_t n = fread(buf, 1, CFG_LEN + 1, f);
    fclose(f);
    if (n != CFG_LEN) { printf("userConfig: tamanho %zu != %d, usando RAM\n", n, CFG_LEN); return -1; }
    if (stored() != crc(buf)) { printf("userConfig: CRC invalido, usando RAM\n"); return -1; }
    for (int i = 0; i < N; i++) if (OFF[i]) v[i] = buf[OFF[i]];
    return 0;
}

static void init(void)
{
    if (loaded) return;
    loaded = 1;
    valid = !load(CFG_IN);
}

/* Opcao 3 "Botao auxiliar" = o NOSSO botao (touchfix.so), nao o flutuante do cdr (0x38 fica como o dono deixou, desligado).
 * Desligado = existe /data/aux_off. */
#define AUX_ID 3
#define AUX_OFF "/data/aux_off"
/* Opcao 21 "Apagar gravacoes antigas" = tools/sd_clean.sh; desligada = existe /data/sd_loop_off. */
#define SDL_ID 21
#define SDL_OFF "/data/sd_loop_off"
/* Opcoes 22/23 = cor principal (0 Preto 1 Azul 2 Branco 3 Bege) e secundaria (0 Branco 1 Verde 2 Vermelho 3 Preto 4 Azul 5 Bege): "PS" em /data/theme */
#define THEME_FILE "/data/theme"
static int th_get(int s)
{
    char b[8] = "";
    FILE *f = fopen(THEME_FILE, "r");
    if (f) fgets(b, sizeof b, f), fclose(f);
    int d = b[s] - 48;
    return d >= 0 && d <= 9 ? d : 0;
}
static void th_set(int s, int val)
{
    char b[4] = {'0' + th_get(0), '0' + th_get(1), 10, 0};
    b[s] = '0' + val;
    FILE *f = fopen(THEME_FILE, "w");
    if (f) fputs(b, f), fclose(f);
}
int hw_config_mapped(int id) { return (id >= 0 && id < N && OFF[id]) || id == AUX_ID || id == SDL_ID || id == 22 || id == 23; }

int hw_config_get(int id)
{
    init();
    if (id == AUX_ID) return access(AUX_OFF, F_OK) != 0;
    if (id == SDL_ID) return access(SDL_OFF, F_OK) != 0;
    if (id == 22 || id == 23) return th_get(id - 22);
    return id >= 0 && id < N ? v[id] : 0;
}

void hw_config_set(int id, int val)
{
    init();
    if (id < 0 || id >= N) return;
    if (id == AUX_ID) {
        if (val) unlink(AUX_OFF);
        else close(open(AUX_OFF, O_WRONLY | O_CREAT, 0644));
        printf("botao auxiliar = %d\n", val);
        return;
    }
    if (id == 22 || id == 23) { th_set(id - 22, val); return; }
    if (id == SDL_ID) {
        if (val) unlink(SDL_OFF);
        else close(open(SDL_OFF, O_WRONLY | O_CREAT, 0644));
        printf("limpeza do SD = %d\n", val);
        return;
    }
    v[id] = val;
    if (!OFF[id] || !valid) { printf("RAM: config %d = %d\n", id, val); return; }
    buf[OFF[id]] = val;
    uint32_t c = crc(buf);
    for (int i = 0; i < 4; i++) buf[i] = c >> (8 * i);
    int fd = open(CFG_OUT ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int ok = fd >= 0 && write(fd, buf, CFG_LEN) == CFG_LEN && !fsync(fd);
    if (fd >= 0) close(fd);
    ok = ok && !rename(CFG_OUT ".tmp", CFG_OUT);
    printf("config %d = %d -> %s %s\n", id, val, CFG_OUT, ok ? "ok" : "ERRO");
}

/* Acoes destrutivas (ja confirmadas em 2 passos na tela): 14 = limpar historico de conexoes, 17 = formatar o cartao.
 * Restaurar fabrica (18) esta fora da grade. Tudo no tools/act.sh (/data/act.sh). */
int hw_config_do(int id)
{
    const char *a = id == 14 ? "clear_conn" : id == 17 ? "format_sd" : NULL;
    if (!a) { printf("acao %d sem funcao%c", id, 10); return 0; }
    char c[64];
    snprintf(c, sizeof c, "sh /data/act.sh %s >/dev/null 2>&1 &", a);
    printf("acao %s -> %d%c", a, system(c), 10);
    return 0;
}

/* --cfgtest: confere CRC e imprime cada opcao mapeada */
int hw_config_test(const char *path)
{
    int r = load(path);
    if (!r) printf("CRC ok\n");
    for (int i = 0; i < N; i++) if (OFF[i]) printf("opt %d (0x%02X) = %d\n", i, OFF[i], v[i]);
    return r ? 1 : 0;
}
