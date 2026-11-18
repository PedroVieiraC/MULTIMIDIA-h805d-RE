/* Mixer ALSA sem alsa-utils: so ioctls de controle em /dev/snd/controlC<n>.
 * mixer_list(): imprime numid, nome, tipo, faixa e valor de todos os controles de todas as placas.
 * mixer_set_pct(name, pct): grava pct (0-100) em todo controle INTEGER chamado `name`
 *   (BOOLEAN: pct>0 liga). Retorna quantos controles gravou, ou -1 se nenhum.
 * Teste isolado (so no PC/aparelho): -DMIXER_MAIN -> `mixer` lista, `mixer "LINEOUT volume" 60`.
 * shortcut: so INTEGER/BOOLEAN; ENUM (ex. "LINEOUT Output Select") fica so no mixer_list. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

#define MIXER_MAX_CARDS 4

static int ctl_open(int card)
{
    char p[32];
    snprintf(p, sizeof p, "/dev/snd/controlC%d", card);
    return open(p, O_RDWR);
}

/* Devolve a lista de ids (malloc) e o total em *n, ou NULL. */
static struct snd_ctl_elem_id *ctl_ids(int fd, unsigned *n)
{
    struct snd_ctl_elem_list l;
    memset(&l, 0, sizeof l);
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) < 0 || l.count == 0) return NULL;
    struct snd_ctl_elem_id *ids = calloc(l.count, sizeof *ids);
    if (!ids) return NULL;
    l.space = l.count;
    l.pids = ids;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) < 0) { free(ids); return NULL; }
    *n = l.used;
    return ids;
}

static const char *type_name(int t)
{
    switch (t) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN: return "bool";
    case SNDRV_CTL_ELEM_TYPE_INTEGER: return "int";
    case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "enum";
    case SNDRV_CTL_ELEM_TYPE_BYTES: return "bytes";
    case SNDRV_CTL_ELEM_TYPE_IEC958: return "iec958";
    case SNDRV_CTL_ELEM_TYPE_INTEGER64: return "int64";
    default: return "?";
    }
}

static void print_val(const struct snd_ctl_elem_info *in, const struct snd_ctl_elem_value *v)
{
    unsigned c = in->count > 8 ? 8 : in->count;
    for (unsigned i = 0; i < c; i++) {
        switch (in->type) {
        case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
        case SNDRV_CTL_ELEM_TYPE_INTEGER: printf(" %ld", v->value.integer.value[i]); break;
        case SNDRV_CTL_ELEM_TYPE_INTEGER64: printf(" %lld", (long long)v->value.integer64.value[i]); break;
        case SNDRV_CTL_ELEM_TYPE_ENUMERATED: printf(" %u", v->value.enumerated.item[i]); break;
        default: break;
        }
    }
}

int mixer_list(void)
{
    int found = 0;
    for (int card = 0; card < MIXER_MAX_CARDS; card++) {
        int fd = ctl_open(card);
        if (fd < 0) continue;
        unsigned n = 0;
        struct snd_ctl_elem_id *ids = ctl_ids(fd, &n);
        printf("== card %d: %u controles\n", card, ids ? n : 0);
        for (unsigned i = 0; ids && i < n; i++) {
            static struct snd_ctl_elem_info in;
            static struct snd_ctl_elem_value v;
            memset(&in, 0, sizeof in);
            in.id = ids[i];
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &in) < 0) continue;
            printf("c%d numid=%u '%s' %s x%u", card, ids[i].numid, (const char *)ids[i].name, type_name(in.type), in.count);
            if (in.type == SNDRV_CTL_ELEM_TYPE_INTEGER)
                printf(" [%ld..%ld step %ld]", in.value.integer.min, in.value.integer.max, in.value.integer.step);
            else if (in.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
                printf(" {");
                for (unsigned k = 0; k < in.value.enumerated.items && k < 16; k++) {
                    in.value.enumerated.item = k; /* ELEM_INFO devolve o nome do item pedido */
                    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &in) == 0) printf("%u=%s ", k, in.value.enumerated.name);
                }
                printf("}");
            }
            memset(&v, 0, sizeof v);
            v.id = ids[i];
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) == 0) { printf(" ="); print_val(&in, &v); }
            printf("\n");
            found++;
        }
        free(ids);
        close(fd);
    }
    return found ? 0 : -1;
}

int mixer_set_pct(const char *name, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int wrote = 0;
    for (int card = 0; card < MIXER_MAX_CARDS; card++) {
        int fd = ctl_open(card);
        if (fd < 0) continue;
        unsigned n = 0;
        struct snd_ctl_elem_id *ids = ctl_ids(fd, &n);
        for (unsigned i = 0; ids && i < n; i++) {
            if (strcmp((const char *)ids[i].name, name) != 0) continue;
            static struct snd_ctl_elem_info in;
            static struct snd_ctl_elem_value v;
            memset(&in, 0, sizeof in);
            in.id = ids[i];
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &in) < 0) continue;
            if (in.type != SNDRV_CTL_ELEM_TYPE_INTEGER && in.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN) continue;
            memset(&v, 0, sizeof v);
            v.id = ids[i];
            long x = pct > 0;
            if (in.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
                long mn = in.value.integer.min, mx = in.value.integer.max;
                x = mn + ((mx - mn) * pct + 50) / 100;
            }
            for (unsigned k = 0; k < in.count && k < 128; k++) v.value.integer.value[k] = x;
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v) == 0) wrote++;
            else perror("ELEM_WRITE");
        }
        free(ids);
        close(fd);
    }
    return wrote ? wrote : -1;
}

#ifdef MIXER_MAIN
int main(int argc, char **argv)
{
    if (argc == 3) return mixer_set_pct(argv[1], atoi(argv[2])) < 0;
    return mixer_list() != 0;
}
#endif

/* valor atual em % (0..100) do 1o controle inteiro com esse nome; -1 se nao achar */
int mixer_get_pct(const char *name)
{
    for (int card = 0; card < MIXER_MAX_CARDS; card++) {
        int fd = ctl_open(card);
        if (fd < 0) continue;
        unsigned n = 0;
        struct snd_ctl_elem_id *ids = ctl_ids(fd, &n);
        for (unsigned i = 0; ids && i < n; i++) {
            if (strcmp((const char *)ids[i].name, name) != 0) continue;
            static struct snd_ctl_elem_info in;
            static struct snd_ctl_elem_value v;
            memset(&in, 0, sizeof in);
            in.id = ids[i];
            memset(&v, 0, sizeof v);
            v.id = ids[i];
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &in) < 0 || in.type != SNDRV_CTL_ELEM_TYPE_INTEGER ||
                ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) continue;
            long mn = in.value.integer.min, mx = in.value.integer.max;
            free(ids);
            close(fd);
            return mx > mn ? (int)((v.value.integer.value[0] - mn) * 100 / (mx - mn)) : -1;
        }
        free(ids);
        close(fd);
    }
    return -1;
}
