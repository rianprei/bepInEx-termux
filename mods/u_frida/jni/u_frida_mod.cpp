// u_frida — carrega scripts Frida .js como mod (FASE F11, runtime-only).
// O usuário solta meu_mod.js na pasta do jogo; este .so escreve o config
// JSON do gadget (modo script-directory) e dá dlopen no frida-gadget.bin
// (que o Manager copiou junto). Sem PC, sem frida-server, sem patch de APK.
// Log mínimo próprio (F2 dá mod_common.h e a gente troca).
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "u_frida_config.h"

#define UF_TAG "u_frida"
#define UF_MODS_DIR_FMT "/data/local/tmp/mods/%s"
#define UF_LOG_FMT "/data/data/%s/files/bepinex/log.txt"

static char uf_pkg[128];
static char uf_dir[320];

// Log mínimo C1: logcat + append em log.txt (HH:MM:SS [u_frida] msg, 256KB).
static void uf_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, UF_TAG, "%s", msg);
    if (!uf_pkg[0]) return;
    char path[384];
    snprintf(path, sizeof(path), UF_LOG_FMT, uf_pkg);
    char dir[384];
    snprintf(dir, sizeof(dir), UF_LOG_FMT, uf_pkg);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    for (char *p = dir + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
    }
    mkdir(dir, 0755);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) > 256 * 1024) {  // C1: corta em 256KB
        fclose(f);
        f = fopen(path, "w");
        if (!f) return;
    }
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    fprintf(f, "%02d:%02d:%02d [%s] %s\n",
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, UF_TAG, msg);
    fclose(f);
}

static bool uf_is_zygote(const char *s) {
    return s && strncmp(s, "zygote", 6) == 0;
}

// Pacote (C1): getenv("BEPINEX_PKG"); fallback cmdline fora de zygote*.
static bool uf_read_pkg() {
    const char *env = getenv("BEPINEX_PKG");
    if (env && env[0] && !uf_is_zygote(env)) {
        snprintf(uf_pkg, sizeof(uf_pkg), "%s", env);
    } else {
        FILE *f = fopen("/proc/self/cmdline", "r");
        if (!f) return false;
        char cmd[128] = {};
        size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
        fclose(f);
        if (n == 0 || uf_is_zygote(cmd)) return false;
        snprintf(uf_pkg, sizeof(uf_pkg), "%s", cmd);
    }
    snprintf(uf_dir, sizeof(uf_dir), UF_MODS_DIR_FMT, uf_pkg);
    return true;
}

static void *uf_worker(void *) {
    int waits = 0;
    while (!uf_pkg[0] && !uf_read_pkg()) {
        if (++waits % 60 == 1) uf_log("sem pacote resolvido ainda, aguardando");
        sleep(1);
    }
    // Tem .js? Sem script, o gadget nem carrega (nada pra rodar).
    struct dirent **ents = nullptr;
    int n = scandir(uf_dir, &ents, nullptr, alphasort);
    if (n < 0) { uf_log("dir %s ausente", uf_dir); return nullptr; }
    const char *first_js = nullptr;
    static char js_name[256];
    for (int i = 0; i < n; i++) {
        if (uf_is_js_mod(ents[i]->d_name) && !first_js) {
            snprintf(js_name, sizeof(js_name), "%s", ents[i]->d_name);
            first_js = js_name;
        }
        free(ents[i]);
    }
    free(ents);
    if (!first_js) { uf_log("sem *.js em %s, gadget não carregado", uf_dir); return nullptr; }
    char gadget[448], config[448];
    snprintf(gadget, sizeof(gadget), "%s/%s", uf_dir, UF_GADGET_FILE);
    snprintf(config, sizeof(config), "%s/%s", uf_dir, UF_CONFIG_FILE);
    struct stat st;
    if (stat(gadget, &st) != 0) {
        uf_log("sem %s na pasta (Manager copia junto com o .js)", UF_GADGET_FILE);
        return nullptr;
    }
    // Config ANTES do dlopen: o constructor do gadget lê na carga.
    char json[512];
    uf_build_config(json, sizeof(json), uf_dir);
    FILE *f = fopen(config, "w");
    if (!f) { uf_log("não consegui escrever %s", UF_CONFIG_FILE); return nullptr; }
    fputs(json, f);
    fclose(f);
    // stdout do jogo vai pra /dev/null: console.log do script não aparece
    // no logcat. Script de teste tem que fazer efeito observável (escrever
    // arquivo, mudar comportamento), não só logar.
    void *h = dlopen(gadget, RTLD_NOW);
    if (!h) { uf_log("dlopen do gadget falhou: %s", dlerror()); return nullptr; }
    uf_log("gadget ativo (script-directory em %s, 1º script %s)", uf_dir, first_js);
    return nullptr;
}

__attribute__((constructor)) static void u_frida_init() {
    uf_read_pkg();
    pthread_t t;
    if (pthread_create(&t, nullptr, uf_worker, nullptr) == 0) pthread_detach(t);
}
