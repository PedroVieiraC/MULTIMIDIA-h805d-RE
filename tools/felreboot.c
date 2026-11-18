#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define MAGIC1   0xfee1dead
#define MAGIC2   672274793      /* 0x28121969 */
#define RESTART2 0xA1B2C3D4

/* reboot(2) RESTART2 com string de modo (default "efex"); o handler de
   restart do sunxi traduz no flag do RTC GPR (0x07090118) -> entra em FEL.
   "--dry": so testa que o binario executa, sem reiniciar. */
int main(int argc, char **argv) {
    const char *mode = (argc > 1) ? argv[1] : "efex";
    if (strcmp(mode, "--dry") == 0) {
        printf("felreboot: dry-run OK (arch/libc ok)\n");
        return 0;
    }
    printf("felreboot: RESTART2 mode='%s'\n", mode);
    fflush(stdout);
    sync();
    sleep(1);
    long r = syscall(SYS_reboot, MAGIC1, MAGIC2, RESTART2, (void *)mode);
    printf("felreboot: syscall retornou %ld (NAO reiniciou)\n", r);
    return 1;
}
