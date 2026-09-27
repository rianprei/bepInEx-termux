// u_frida — carrega scripts Frida .js como mod (FASE F11, runtime-only).
// O usuário solta meu_mod.js na pasta do jogo; este .so SÓ verifica
// frida-gadget.bin + frida-gadget.config ao lado e dá dlopen no binário
// se o config estiver no modo script. Sem frida-server, sem patch de APK.
// Pacote, dir e log: mods/common/mod_common.h (C1).
//
// Onde mora o quê: binário E config ficam na pasta de mods
// (/data/local/tmp/mods/<pkg>/), que é bepinex_mod_file e tem
// `allow appdomain bepinex_mod_file file { getattr open read map execute }`
// no module/sepolicy.rule. [NAO VERIFICADO EM ENFORCING] dlopen a partir
// de /data/data/<pkg>/files (app_data_file) seria negado: AOSP
// private/app.te só dá create_file_perms (sem execute) em app_data_file —
// o teste de device foi em Permissive. Hoje quem instala é só o
// tools/deploy_frida.sh (PC + adb + su): copia o .bin e ESCREVE o .config;
// o jogo só lê e executa. O gadget deriva o config do próprio caminho do
// binário (gadget.vala:2016-2028).
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../common/il2cpp_min.h"
#include "../../common/mod_common.h"
#include "u_frida_config.h"

#define UF_TAG "u_frida"

static char uf_dir[320];

// Lê o config inteiro (até UF_CONFIG_MAX + 1, pra detectar o que passa do
// teto). -1 = não abriu/leu (errno preservado).
static ssize_t uf_read_config(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < size) {
        ssize_t r = read(fd, buf + got, size - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            close(fd);
            errno = e;
            return -1;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    return (ssize_t)got;
}

static bool uf_gadget_matches_process_abi(const char *path) {
    unsigned char header[20];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < sizeof(header)) {
        ssize_t n = read(fd, header + got, sizeof(header) - got);
        if (n <= 0) { close(fd); return false; }
        got += (size_t)n;
    }
    close(fd);
#if defined(__aarch64__)
    return uf_elf_matches_abi(header, sizeof(header), 2, 183);
#elif defined(__arm__)
    return uf_elf_matches_abi(header, sizeof(header), 1, 40);
#else
    return false;
#endif
}

static void *uf_worker(void *) {
    // Tem .js? Sem script, o gadget nem carrega (nada pra rodar).
    struct dirent **ents = nullptr;
    int n = scandir(uf_dir, &ents, nullptr, alphasort);
    if (n < 0) { mod_log(UF_TAG, "dir %s ausente", uf_dir); return nullptr; }
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
    if (!first_js) { mod_log(UF_TAG, "sem *.js em %s, gadget não carregado", uf_dir); return nullptr; }

    // Binário + config TÊM que estar na pasta de mods (bepinex_mod_file,
    // com map+execute). O jogo só verifica: quem instala
    // (tools/deploy_frida.sh via su) copia o .bin e escreve o .config.
    char gadget[448], cfg[448];
    snprintf(gadget, sizeof(gadget), "%s/%s", uf_dir, UF_GADGET_FILE);
    snprintf(cfg, sizeof(cfg), "%s/%s", uf_dir, UF_CONFIG_FILE);
    struct stat st;
    if (stat(gadget, &st) != 0) {
        mod_log(UF_TAG, "sem %s na pasta (tools/deploy_frida.sh instala) — gadget NAO carregado", UF_GADGET_FILE);
        return nullptr;
    }
    if (!uf_gadget_matches_process_abi(gadget)) {
        mod_log(UF_TAG, "%s não corresponde à ABI do processo — gadget NAO carregado", gadget);
        return nullptr;
    }
    // Config: ausente/ilegível e fora do modo script são mensagens
    // diferentes. Só dlopen no modo script: o default do gadget (listen +
    // on_load wait) congela o jogo esperando cliente e abre socket.
    static char cbuf[UF_CONFIG_MAX + 1];
    ssize_t cn = uf_read_config(cfg, cbuf, sizeof(cbuf));
    if (cn < 0) {
        mod_log(UF_TAG, "sem %s legível (%s) — gadget NAO carregado", UF_CONFIG_FILE, strerror(errno));
        return nullptr;
    }
    char why[64];
    if (!uf_config_is_script_mode(cbuf, (size_t)cn, why, sizeof(why))) {
        mod_log(UF_TAG, "config fora do modo script (type=%s) — gadget NAO carregado", why);
        return nullptr;
    }

    // O gadget também pode rodar scripts em jogos sem IL2CPP; mantém 10s de
    // orçamento total para não atrasar esse caso. Scripts que usam Il2Cpp.*
    // só são compatíveis se o boot comum terminar dentro dessa janela.
    static const mod_il2cpp_wait_policy frida_wait = mod_il2cpp_frida_wait_policy();
    Il2Cpp il;
    if (il2cpp_boot(il, &frida_wait)) {
        mod_log(UF_TAG, "IL2CPP pronto em até 10s; carregando gadget");
    } else {
        mod_log(UF_TAG, "gadget será carregado sem IL2CPP após 10s; scripts Il2Cpp.* "
                        "não terão runtime pronto");
    }
    // stdout do jogo vai pra /dev/null: console.log do script não aparece
    // no logcat. Script de teste tem que fazer efeito observável (escrever
    // arquivo, mudar comportamento), não só logar.
    void *h = dlopen(gadget, RTLD_NOW);
    if (!h) { mod_log(UF_TAG, "dlopen do gadget falhou: %s", dlerror()); return nullptr; }
    mod_log(UF_TAG, "gadget ativo (type=%s, pasta %s, 1º script %s)", why, uf_dir, first_js);
    return nullptr;
}

// Pacote no constructor (BEPINEX_PKG do loader; cmdline só fora de
// zygote*). Sem pacote usável não há thread nem loop — dormir no dlopen
// atrasaria o specialize. Pacote fora da regra (ex: '/', "..") = inerte,
// só logcat: nem o log.txt usa um caminho montado dele.
__attribute__((constructor)) static void u_frida_init() {
    const char *pkg = mod_pkg();
    if (!pkg) {
        __android_log_print(ANDROID_LOG_INFO, UF_TAG, "sem pacote (sem BEPINEX_PKG) — inerte");
        return;
    }
    if (!uf_pkg_ok(pkg) || !mod_dir(uf_dir, sizeof(uf_dir))) {
        __android_log_print(ANDROID_LOG_WARN, UF_TAG, "pacote inválido '%s' — inerte", pkg);
        return;
    }
    pthread_t t;
    if (pthread_create(&t, nullptr, uf_worker, nullptr) == 0) pthread_detach(t);
}
