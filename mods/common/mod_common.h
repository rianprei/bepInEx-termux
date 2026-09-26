// mod_common.h — SDK mínimo dos mods do caminho genérico (mods/<pkg>/):
// pacote do jogo (C1), dir de mods, log C1 e opções .conf (C3). Header-only:
// cada mod inclui e chama da própria thread de trabalho.
//
// Pacote (C1, atualizado 2026-09-26): o loader exporta BEPINEX_PKG antes do
// dlopen — é a fonte certa já no constructor, quando /proc/self/cmdline
// ainda vale "zygote64" (o processo não especializou; o u_patch chegou a
// instalar em mods/zygote64 por ler cmdline cedo demais). O cmdline só
// serve de fallback DEPOIS de sair de zygote*: a 1ª chamada pode devolver
// nullptr e a seguinte acertar (não há cache negativo).
//
// mod_log: mesma linha do loader (jni/main.cpp::pkg_log_line) —
// "HH:MM:SS [tag] msg" em /data/data/<pkg>/files/bepinex/log.txt, append,
// zera ao passar de 256KB. Cria files/ (0771) e bepinex/ (0700) se faltarem.
// Sem pkg conhecido: só logcat. Falha de log nunca derruba o mod.
//
// Compila no host (g++ -Wall -Wextra) para teste: logcat vira stderr e as
// partes de device (paths /data) degradam em silêncio.
#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

// ---- pacote do jogo (C1) ----

// Núcleo puro (testável no host): 1º argumento do /proc/self/cmdline, com
// buf/n vindos do read() real. Rejeita zygote* e buffer que não caiba.
static inline bool mod_pkg_from_cmdline(const char *buf, size_t n, char *out, size_t size) {
    if (n == 0 || n >= size) return false;
    memcpy(out, buf, n);
    out[n] = '\0';
    return out[0] != '\0' && strncmp(out, "zygote", 6) != 0;
}

// Pacote do jogo, ou nullptr se ainda não souber (env ausente + processo
// ainda zygote*). Só o fallback é cacheado (a env nunca muda após o dlopen).
static inline const char *mod_pkg(void) {
    const char *env = getenv("BEPINEX_PKG");
    if (env && *env) return env;
    static char pkg[128];
    static bool done;
    if (!done) {
        int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char buf[sizeof(pkg)];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) done = mod_pkg_from_cmdline(buf, (size_t)n, pkg, sizeof(pkg));
        }
    }
    return done ? pkg : nullptr;
}

// ---- dir de mods (C1) ----

// /data/local/tmp/mods/<pkg>/ em out. false se o pacote ainda não é conhecido
// (constructor sem env, antes do specialize — chame da thread de trabalho).
static inline bool mod_dir(char *out, size_t size) {
    const char *pkg = mod_pkg();
    if (!pkg) return false;
    int n = snprintf(out, size, "/data/local/tmp/mods/%s", pkg);
    return n > 0 && (size_t)n < size;
}

// ---- log (C1) ----

#define MOD_LOG_CAP (256 * 1024)

// Parte em arquivo do mod_log. Recebe va_list e não o consome (quem chama
// dá va_start/va_end) — o padrão printf(3).
static inline void mod_log_file(const char *tag, const char *fmt, va_list ap) {
    const char *pkg = mod_pkg();
    if (!pkg) return;
    // files/ pode não existir ainda (app que nunca chamou getFilesDir):
    // 0771 é a permissão que o próprio Android dá pro files dir do app.
    char dir[336];
    int n = snprintf(dir, sizeof(dir), "/data/data/%s/files", pkg);
    if (n <= 0 || (size_t)n >= sizeof(dir)) return;
    if (mkdir(dir, 0771) != 0 && errno != EEXIST) return;
    size_t base = (size_t)n;
    if ((size_t)snprintf(dir + base, sizeof(dir) - base, "/bepinex") >= sizeof(dir) - base) return;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return;
    base = strlen(dir);
    char path[352];
    if ((size_t)snprintf(path, sizeof(path), "%s/log.txt", dir) >= sizeof(path)) return;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;
    // Sem rotação: passou do teto, zera (mesma semântica do loader).
    if (lseek(fd, 0, SEEK_END) >= MOD_LOG_CAP) {
        if (ftruncate(fd, 0) != 0) { /* segue: só não zera */ }
    }
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char stamp[16], body[512], line[640];
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm_now);
    vsnprintf(body, sizeof(body), fmt, ap);
    n = snprintf(line, sizeof(line), "%s [%s] %s\n", stamp, tag, body);
    if (n > 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
    }
    close(fd);
}

// Log do mod: logcat E linha C1 no log.txt do jogo. Nunca derruba por falha.
static inline void mod_log(const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, tag, fmt, ap);
#else
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
#endif
    va_end(ap);
    va_start(ap, fmt);
    mod_log_file(tag, fmt, ap);
    va_end(ap);
}

// ---- opções .conf (C3) ----

// Núcleo puro (testável no host): procura key no .conf aberto. Uma linha
// "key=value", '#' comenta; espaços em volta do '=' e no fim do valor são
// ignorados; valor pode ser vazio ("key="). Relê do início a cada chamada
// (opções são lidas no boot, não é caminho quente).
static inline bool mod_conf_find(FILE *f, const char *key, char *out, size_t size) {
    size_t klen = strlen(key);
    char line[256];
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || strncmp(p, key, klen) != 0) continue;
        p += klen;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        snprintf(out, size, "%s", p);
        return true;
    }
    return false;
}

// Valor de key no <id>.conf do dir do mod (C3; o Manager instala as opções
// junto com o .so). def se o arquivo ou a key não existir. O retorno vive em
// buffer estático — copie se precisar dele depois da próxima chamada.
static inline const char *mod_conf_get(const char *id, const char *key, const char *def) {
    static char value[128];
    char dir[320];
    if (!mod_dir(dir, sizeof(dir))) return def;
    char path[384];
    int n = snprintf(path, sizeof(path), "%s/%s.conf", dir, id);
    if (n <= 0 || (size_t)n >= sizeof(path)) return def;
    FILE *f = fopen(path, "re");
    if (!f) return def;
    bool found = mod_conf_find(f, key, value, sizeof(value));
    fclose(f);
    return found ? value : def;
}

// Conversões pros tipos de option do manifest (C2). Núcleos puros: valor
// ausente, vazio ou que não parseie = default, sem erro (regra ruim não
// derruba mod).
static inline bool conf_as_bool(const char *v, bool def) {
    if (!v || !*v) return def;
    return v[0] == 't' || v[0] == 'T' || v[0] == '1' || v[0] == 'y' || v[0] == 'Y';
}

static inline long conf_as_int(const char *v, long def) {
    if (!v || !*v) return def;
    char *end;
    long r = strtol(v, &end, 10);
    return end == v ? def : r;
}

static inline double conf_as_float(const char *v, double def) {
    if (!v || !*v) return def;
    char *end;
    double r = strtod(v, &end);
    return end == v ? def : r;
}

// Leitura já convertida do <id>.conf (C3).
static inline bool mod_conf_bool(const char *id, const char *key, bool def) {
    return conf_as_bool(mod_conf_get(id, key, nullptr), def);
}

static inline long mod_conf_int(const char *id, const char *key, long def) {
    return conf_as_int(mod_conf_get(id, key, nullptr), def);
}

static inline double mod_conf_float(const char *id, const char *key, double def) {
    return conf_as_float(mod_conf_get(id, key, nullptr), def);
}
