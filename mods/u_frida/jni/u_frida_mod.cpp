// u_frida — carrega scripts Frida .js como mod (FASE F11, runtime-only).
// O usuário solta meu_mod.js na pasta do jogo; este .so SÓ verifica
// frida-gadget.bin + frida-gadget.config ao lado e dá dlopen no binário.
// Sem PC, sem frida-server, sem patch de APK. Log mínimo próprio (F2 dá
// mod_common.h e a gente troca).
//
// Onde mora o quê (re-review: Enforcing): binário E config ficam na pasta
// de mods (/data/local/tmp/mods/<pkg>/), que é bepinex_mod_file e tem
// map+execute no sepolicy.rule. dlopen a partir de
// /data/data/<pkg>/files (app_data_file) é NEGADO em Enforcing — AOSP
// private/app.te não dá execute em app_data_file pro appdomain (passava
// só porque o device de teste está Permissive). Quem instala
// (Manager/deploy via su) copia o .bin e ESCREVE o .config com os caminhos
// dos .js; o jogo só lê e executa. O gadget deriva o config do próprio
// caminho do binário (gadget.vala:2016-2028).
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../../common/il2cpp_min.h"
#include "u_frida_config.h"

#define UF_TAG "u_frida"
#define UF_MODS_DIR_FMT "/data/local/tmp/mods/%s"
#define UF_OUT_DIR_FMT "/data/data/%s/files/bepinex"
#define UF_LOG_FMT "/data/data/%s/files/bepinex/log.txt"
#define UF_LOG_MAX (256u * 1024u)
#define UF_LOG_KEEP (128u * 1024u)

static char uf_pkg[128];
static char uf_dir[320];

// Log mínimo C1: logcat + append em log.txt (HH:MM:SS [u_frida] msg).
// Teto 256KB mantendo a METADE final; a linha entra num write() único com
// O_APPEND (mesma política do u_patch).
static void uf_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, UF_TAG, "%s", msg);
    if (!uf_pkg[0]) return;
    char path[384], files[384], dir[384];
    snprintf(path, sizeof(path), UF_LOG_FMT, uf_pkg);
    snprintf(files, sizeof(files), "/data/data/%s/files", uf_pkg);
    snprintf(dir, sizeof(dir), UF_OUT_DIR_FMT, uf_pkg);
    mkdir(files, 0771);  // files/ antes de bepinex/ (dono é o app, pode)
    mkdir(dir, 0755);
    struct stat st;
    if (stat(path, &st) == 0 && (uint64_t)st.st_size > UF_LOG_MAX) {
        FILE *rf = fopen(path, "r");
        if (rf) {
            char *tail = (char *)malloc(UF_LOG_KEEP);
            size_t got = 0;
            if (tail) {
                fseek(rf, st.st_size - (off_t)UF_LOG_KEEP, SEEK_SET);
                got = fread(tail, 1, UF_LOG_KEEP, rf);
            }
            fclose(rf);
            FILE *wf = fopen(path, "w");
            if (wf) {
                if (got) fwrite(tail, 1, got, wf);
                fclose(wf);
            }
            free(tail);
        }
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char line[640];
    int n = snprintf(line, sizeof(line), "%02d:%02d:%02d [%s] %s\n",
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec, UF_TAG, msg);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        write(fd, line, len);  // write único
    }
    close(fd);
}

static bool uf_is_zygote(const char *s) {
    return s && strncmp(s, "zygote", 6) == 0;
}

// Pacote UMA vez, no constructor (revisão kilo-15): env do loader, senão
// UMA leitura de cmdline (tardia = válida; cedo = zygote = inútil). Sem
// pacote não há thread nem loop — dormir no dlopen atrasaria o specialize.
static bool uf_read_pkg_once() {
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

static int uf_has_il2cpp_cb(struct dl_phdr_info *info, size_t, void *out) {
    if (info->dlpi_name && strstr(info->dlpi_name, "/libil2cpp.so")) {
        *(int *)out = 1;
        return 1;
    }
    return 0;
}

static void *uf_worker(void *) {
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

    // Binário + config TÊM que estar na pasta de mods (bepinex_mod_file,
    // com map+execute). O jogo só verifica: quem instala (Manager/deploy
    // via su) copia o .bin e escreve o .config. Sem .bin/.config válido,
    // nada carrega (log explica).
    char gadget[448], cfg[448];
    snprintf(gadget, sizeof(gadget), "%s/%s", uf_dir, UF_GADGET_FILE);
    snprintf(cfg, sizeof(cfg), "%s/%s", uf_dir, UF_CONFIG_FILE);
    struct stat st;
    if (stat(gadget, &st) != 0) {
        uf_log("sem %s na pasta (quem instala copia via su+chcon)", UF_GADGET_FILE);
        return nullptr;
    }
    if (stat(cfg, &st) != 0 || st.st_size == 0) {
        uf_log("sem %s válido (quem instala escreve via su+chcon)", UF_CONFIG_FILE);
        return nullptr;
    }
    FILE *cf = fopen(cfg, "r");
    int first = cf ? fgetc(cf) : EOF;
    if (cf) fclose(cf);
    if (first != '{') {
        uf_log("%s não é JSON (primeiro byte %d), gadget não carregado", UF_CONFIG_FILE, first);
        return nullptr;
    }

    // Espera o il2cpp antes do dlopen (revisão kilo-14, como o sa2ammo):
    // script Il2Cpp.* carregado antes do init falha. Jogo não-Unity não
    // tem libil2cpp — espera curta (10s) e segue sem a espera, em vez de
    // travar o mod por 4 minutos.
    int seen = 0;
    for (int i = 0; i < 50 && !seen; i++) {
        dl_iterate_phdr(uf_has_il2cpp_cb, &seen);
        if (!seen) usleep(200 * 1000);
    }
    if (seen) {
        Il2Cpp il;
        if (il2cpp_boot(il)) uf_log("il2cpp ok, carregando gadget");
        else uf_log("il2cpp não subiu, carregando gadget mesmo assim");
    } else {
        uf_log("sem libil2cpp em 10s (jogo não-Unity?), gadget sem espera IL2CPP");
    }
    // stdout do jogo vai pra /dev/null: console.log do script não aparece
    // no logcat. Script de teste tem que fazer efeito observável (escrever
    // arquivo, mudar comportamento), não só logar.
    void *h = dlopen(gadget, RTLD_NOW);
    if (!h) { uf_log("dlopen do gadget falhou: %s", dlerror()); return nullptr; }
    uf_log("gadget ativo (bin+config em %s, 1º script %s)", uf_dir, first_js);
    return nullptr;
}

__attribute__((constructor)) static void u_frida_init() {
    if (!uf_read_pkg_once()) {
        __android_log_print(ANDROID_LOG_INFO, UF_TAG, "sem pacote (sem BEPINEX_PKG) — inerte");
        return;
    }
    pthread_t t;
    if (pthread_create(&t, nullptr, uf_worker, nullptr) == 0) pthread_detach(t);
}
