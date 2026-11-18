/* Diagnostico: processos do cdr (gocsdk, lylinkapp) mandam o log para /dev/null; aqui o open() de escrita em /dev/null vira /tmp/nl.<pid>.log. Uso: LD_PRELOAD=/tmp/nullog.so cdr */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

__attribute__((constructor)) static void unbuf(void) { setvbuf(stdout, 0, _IONBF, 0), setvbuf(stderr, 0, _IONBF, 0); }

int open(const char *path, int flags, ...)
{
    static int (*real)(const char *, int, ...);
    if (!real) real = dlsym(RTLD_NEXT, "open");
    va_list ap;
    va_start(ap, flags);
    int mode = va_arg(ap, int);
    va_end(ap);
    if (path && !strcmp(path, "/dev/null") && (flags & 3) != O_RDONLY) {
        char b[40] = "/tmp/nl.";
        int n = 8, pid = getpid(), d = 1;
        while (pid / d >= 10) d *= 10;
        for (; d; d /= 10) b[n++] = '0' + pid / d % 10, pid %= d;
        memcpy(b + n, ".log", 5);
        int fd = real(b, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) return fd;
    }
    return real(path, flags, mode);
}
int open64(const char *path, int flags, ...) __attribute__((alias("open")));
