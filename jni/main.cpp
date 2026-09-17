// SuperRoot POC v3 hardening: Zygisk module — hooks JNI do Battle Cats via
// Dobby com resiliência. Cada símbolo falha silenciosamente (log) em vez de
// derrubar o processo do jogo.
//
// Alvos (símbolos JNI, confirmados via readelf --dyn-symbols na análise
// original do libnative-lib.so v15.5.0 en; os offsets/build atuais ficam
// em offsetsdb.h, gerado por bc_offset_check.py — não editar aqui):
//   appInit       — chamado 1x no início
//   appUpdateDraw — por frame
//   appTouch      — por toque
//   appKey        — por tecla de hardware
//
// Resiliência: para cada símbolo, se DobbySymbolResolver não achar, loga
// "falhou inspecionando X" e segue pro próximo. Esse padrão permite que
// apenas o hook funcional continue existindo em novas builds do jogo sem
// travar processo — nenhum hook = crash determinístico induzido por nós.

#include <android/log.h>
#include <android/api-level.h>
#include <stdint.h>
#include <dlfcn.h>
#include <link.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <atomic>
#include <sys/socket.h>
#include <errno.h>
#include <sys/system_properties.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dirent.h>

#include "zygisk.hpp"
#include "dobby.h"
#include "offsetsdb.h"   // GERADO por bc_offset_check.py --emit-header (ver context/battlecats-offset-db-schema.md §8)
#include "bc_mods_conf.h" // config runtime de hooks (companion escreve, módulo lê)
#include "bc_hook_logic.h" // dispatcher Prefix/Postfix + lógica unpatch/repatch (single source of truth, testado no harness)
#include "bc_mod_api.h"   // contrato de API exposto aos mods .so dinâmicos
#include "bc_loader.h"    // loader de mods .so (discovery + dlopen + isolamento)
#include "bc_elf_symtab.h"   // enumeração de símbolo ELF dinâmico (generalização Cocos2d-x)
#include "bc_engine_detect.h" // cascata de detecção de engine Cocos2d-x (generalização)
#include "bc_generic_allowlist.h" // allowlist de pacote pra generalização atuar (detecta só nesses)
#include "bc_generic_hook.h" // hook de log genérico (DobbyInstrument) em símbolo Java_* descoberto
#include "bc_mod_graph.h"  // grafo de dependência entre mods (requires/conflicts, topo-sort determinístico)
#include "bc_pattern_scan.h"  // AOB scan — resolve endereço por bytes, sobrevive recompile do jogo

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::Option;

#define LOG_TAG "BCPOC"
#define BC_LOADER_VERSION "v0.3.0"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- Fail-safe state machine (design/zygisk_fallback_design.md) ---
// States: LOADED → ACTIVE | DORMANT
//   ACTIVE  = all hooks installed, forwarding normally
//   DORMANT = at least one hook failed; module stays loaded, logs only
//   UNLOADED = total failure → api->setOption(DLCLOSE_MODULE_LIBRARY)
static std::atomic<bool> g_dormant{false};   // partial failure → skip remaining hooks
static std::atomic<int>  g_sdk{0};           // runtime Android SDK level (JNI)
static std::atomic<bool> g_build_id_resolved{false}; // PT_NOTE build-id == BC_BUILD_ID (fail-closed pra base+offset)
// (g_mod_enabled removido — throttle agora é g_throttle_every:int do config)
static std::atomic<int>  g_frame_counter{0};   // frame counter pro throttle
static std::atomic<int> g_stream_fd{-1};                  // fd do socket pro companion (STREAMING de eventos)
// Métrica de overhead do dispatcher (clock_gettime MONOTONIC)
static std::atomic<uint64_t> g_hook_overhead_ns{0};      // nanos totais no dispatcher
static std::atomic<uint64_t> g_hook_overhead_count{0};   // nº de medições
// Forward decl: publish_log usado por verify_build_id (linha ~166), que vem
// antes da definição real (perto de publish_event, mais abaixo no arquivo).
static void publish_log(const char *level, const char *fmt, ...);

// --- Config runtime por hook (bc_mods.conf) — v2 tipado ---
// Lido no postAppSpecialize (antes da event_thread). Semântica default-
// implícito: chave sem linha no config = valor default da tabela (bools ON,
// throttle 0=off). O companion (root, via Termux autenticado) escreve o
// arquivo; o módulo só lê. Tipos e clamps: bc_mods_conf.h (padrão BepInEx
// AcceptableValue* — valor fora do domínio é COAGIDO, nunca quebra o boot).
#define N_PLANS_MAX 4

// Schema do arquivo (as "ConfigDefinitions" nossas — 1:1 com o que o
// companion aceita escrever). appUpdateDraw substitui a antiga system
// property persist.bc_poc.mod_enabled: int 0 (throttle desligado) ou
// 1..600 (pula 1 frame a cada N).
static const char *const BC_SRC_DOMAIN[] = {"game", "companion", nullptr};
static const struct bc_mod_schema BC_SCHEMA[] = {
    {"appInit",       BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appUpdateDraw", BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},  // gate do hook (bool)
    {"appTouch",      BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appKey",        BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"throttle_every",BC_MOD_INT,  false, 1, 600, 60, nullptr, nullptr}, // 0* não é default válido → default 60, mas 0 desliga: ver g_throttle_every
    {"stream_source", BC_MOD_ENUM, false, 0, 0, 0,    BC_SRC_DOMAIN, "game"},
};
static const int BC_SCHEMA_N = (int)(sizeof(BC_SCHEMA) / sizeof(BC_SCHEMA[0]));

static struct bc_mod_entry g_cfg[8];  // >= BC_SCHEMA_N (6 chaves)
static std::atomic<int> g_mods_count{0};
static std::atomic<int> g_throttle_every{0};  // 0 = throttle desligado; N = pula 1 a cada N frames

// Watch-per-key callbacks (BepInEx SettingChanged port — bc_mods_conf.h).
static bc_mod_watch g_watch_table[BC_SCHEMA_N];
static int g_watch_count = 0;

// lookup simples pós-load (código do hook lê aqui, não o arquivo)
static const struct bc_mod_entry *cfg_find(const char *name) {
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        if (strcmp(g_cfg[i].name, name) == 0) return &g_cfg[i];
    }
    return nullptr;
}

// true = hook habilitado (ou sem entrada no config). Só DESLIGA hooks que o
// config pede explicitamente "off" — fail-open por nome é intencional.
static bool hook_enabled(const char *shortname) {
    const struct bc_mod_entry *e = cfg_find(shortname);
    return (e == nullptr) ? true : e->b;
}

// Lê e parseia o bc_mods.conf pra um array destino (parse puro, sem aplicar).
// Retorna false se o arquivo não existe (destino fica com defaults puros).
static bool read_mods_config_into(struct bc_mod_entry *dst, int cap) {
    int fd = open(BC_MODS_CONF_PATH, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char buf[2048];
        ssize_t total = 0;
        while (total < (ssize_t)sizeof(buf) - 1) {
            ssize_t r = read(fd, buf + total, sizeof(buf) - 1 - (size_t)total);
            if (r < 0) {
                if (errno == EINTR) continue;
                total = -1;
                break;
            }
            if (r == 0) break;
            total += r;
        }
        close(fd);
        if (total < 0) total = 0;
        buf[total] = '\0';
        // v2: parse tipado com defaults preenchidos (present=false)
        bc_mods_parse(buf, BC_SCHEMA, BC_SCHEMA_N, dst, cap);
        return true;
    }
    // arquivo ausente = defaults puros (tudo ON, throttle off)
    bc_mods_parse(nullptr, BC_SCHEMA, BC_SCHEMA_N, dst, cap);
    return false;
}

// Aplica um config já parseado em g_cfg: copia, recalcula derivados (throttle)
// e loga. Caminho único de commit — boot e reload usam o mesmo.
static void apply_mods_config(const struct bc_mod_entry *cfg) {
    memcpy(g_cfg, cfg, sizeof(struct bc_mod_entry) * (size_t)BC_SCHEMA_N);
    g_mods_count.store(BC_SCHEMA_N, std::memory_order_relaxed);
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        switch (BC_SCHEMA[i].type) {
            case BC_MOD_BOOL:
                LOGI("config: %s=%s%s", g_cfg[i].name, g_cfg[i].b ? "on" : "off",
                     g_cfg[i].present ? "" : " (default)");
                break;
            case BC_MOD_INT:
                LOGI("config: %s=%ld%s", g_cfg[i].name, g_cfg[i].i,
                     g_cfg[i].present ? "" : " (default)");
                break;
            case BC_MOD_ENUM:
                LOGI("config: %s=%s%s", g_cfg[i].name, g_cfg[i].s,
                     g_cfg[i].present ? "" : " (default)");
                break;
        }
    }
    // throttle: só ativo se a linha existir E valor >= 1 (0 = desligado)
    const struct bc_mod_entry *th = cfg_find("throttle_every");
    g_throttle_every.store((th != nullptr && th->present && th->i >= 1) ? (int)th->i : 0,
                           std::memory_order_relaxed);
    if (g_throttle_every.load() > 0)
        LOGI("throttle habilitado: pula 1 frame a cada %d", g_throttle_every.load());
}

static void load_mods_config() {
    struct bc_mod_entry novo[BC_SCHEMA_N];
    read_mods_config_into(novo, BC_SCHEMA_N);
    apply_mods_config(novo);
}
// Lê as ELF notes da lib mapeada (PT_NOTE) via dl_iterate_phdr e compara o
// GNU build-id com o BC_BUILD_ID compilado no offsetsdb.h. Se não bater, o
// módulo inteiro vai a DORMANT: nenhum hook por base+offset é aplicado.
// O hook por SÍMBOLO (DobbySymbolResolver) continua permitido — é o caminho
// que sobrevive a lib sem nota/atualizada.
struct BuildIdCtx { const char *libname; char hex[41]; int found; };
static int buildid_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    auto *ctx = static_cast<BuildIdCtx *>(data);
    if (!info->dlpi_name || !strstr(info->dlpi_name, ctx->libname)) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_NOTE) continue;
        const uint8_t *p    = (const uint8_t *)(info->dlpi_addr + ph->p_vaddr);
        const uint8_t *end  = p + ph->p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)p;
            const uint8_t *name  = p + sizeof(ElfW(Nhdr));
            const uint8_t *desc  = name + ((nh->n_namesz + 3) & ~3);
            if (desc + nh->n_descsz > end) break;
            if (nh->n_namesz == 4 && memcmp(name, "GNU\0", 4) == 0 &&
                nh->n_type == NT_GNU_BUILD_ID && nh->n_descsz == 20) {
                static const char *H = "0123456789abcdef";
                for (int b = 0; b < 20; b++) {
                    ctx->hex[b * 2]     = H[desc[b] >> 4];
                    ctx->hex[b * 2 + 1] = H[desc[b] & 0xF];
                }
                ctx->hex[40] = '\0';
                ctx->found = 1;
                return 1;
            }
            p = desc + ((nh->n_descsz + 3) & ~3);
        }
    }
    return 0;
}
// true = build confirmado (ou lib sem nota — só hooks por símbolo rodam)
static bool verify_build_id(const char *libname) {
    BuildIdCtx ctx{libname, {0}, 0};
    dl_iterate_phdr(buildid_phdr_cb, &ctx);
    if (!ctx.found) {
        LOGW("build-id não encontrado em %s — hooks por base+offset desativados", libname);
        return true;
    }
    if (strcmp(ctx.hex, BC_BUILD_ID) != 0) {
        LOGE("build-id MISMATCH: lib=%s esperado=%s — base+offset BLOQUEADO", ctx.hex, BC_BUILD_ID);
        publish_log("Error", "build-id MISMATCH: lib=%s esperado=%s", ctx.hex, BC_BUILD_ID);
        return false;
    }
    LOGI("build-id confirmado: %s", ctx.hex);
    return true;
}

// Opção A (§8): base da lib mapeada — necessário pros alvos base+offset.
// (a get_lib_base do symbol_scan.cpp é static e não entra no build; cópia mínima aqui.)
struct LibBaseCtx { const char *libname; void *base; };
static void *get_lib_base(const char *libname) {
    LibBaseCtx ctx{libname, nullptr};
    dl_iterate_phdr(+[](struct dl_phdr_info *info, size_t, void *d) -> int {
        auto *c = static_cast<LibBaseCtx *>(d);
        if (info->dlpi_name && strstr(info->dlpi_name, c->libname)) {
            c->base = (void *)info->dlpi_addr;
            return 1;  // para na primeira match
        }
        return 0;
    }, &ctx);
    return ctx.base;
}

// --- Fallback de assinatura (context/battlecats-offset-db-schema.md §6) ---
// Compara os primeiros bytes da função contra o prólogo medido no DB
// (offsetsdb.h, campo bytes_prologue). Mascaramento: 0xFF = byte deve bater,
// 0x00 = wildcard. Rodar ANTES do DobbyHook (o hook sobrescreve o prólogo).
static bool prologue_matches(const void *fn, const struct bc_sig *sig) {
    if (sig == nullptr || sig->len == 0) return false;
    const unsigned char *m = (const unsigned char *)fn;
    for (unsigned i = 0; i < sig->len; i++) {
        if ((m[i] & sig->mask[i]) != (sig->bytes[i] & sig->mask[i]))
            return false;
    }
    return true;
}

// Alcance executável da lib no mapa do processo (PT_LOAD com PF_X).
struct LibExecRange { void *base; size_t size; };
static void *get_lib_exec_range(const char *libname, LibExecRange *out) {
    struct Ctx { const char *name; LibExecRange *out; void *base; };
    Ctx ctx{libname, out, nullptr};
    dl_iterate_phdr(+[](struct dl_phdr_info *info, size_t, void *d) -> int {
        auto *c = static_cast<Ctx *>(d);
        if (!info->dlpi_name || !strstr(info->dlpi_name, c->name)) return 0;
        c->base = (void *)info->dlpi_addr;
        for (int i = 0; i < info->dlpi_phnum; i++) {
            const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
            if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X)) {
                c->out->base = (void *)(info->dlpi_addr + ph->p_vaddr);
                // p_memsz inclui BSS (zero-fill além do que o arquivo mapeia
                // de verdade); escanear até lá arrisca ler página não
                // mapeada. min() com p_filesz nunca lê além do garantido
                // (mesmo bug achado e corrigido em bc_pattern_scan.h,
                // review do OpenCode — twin real, mesmo padrão PT_LOAD+PF_X).
                c->out->size = (size_t)(ph->p_filesz < ph->p_memsz ? ph->p_filesz : ph->p_memsz);
            }
        }
        return 1;
    }, &ctx);
    return ctx.base;
}

// Varre o segmento executável atrás do prólogo (passo 4 = alinhamento de
// instrução ARM64). Retorna nullptr com 0 ou 2+ matches (ambíguo = proibido).
static void *scan_exec_unique(const LibExecRange *range, const struct bc_sig *sig) {
    if (range == nullptr || range->base == nullptr || range->size < sig->len)
        return nullptr;
    const unsigned char *lo = (const unsigned char *)range->base;
    const unsigned char *hi = lo + range->size - sig->len;
    void *found = nullptr;
    int hits = 0;
    for (const unsigned char *p = lo; p <= hi; p += 4) {
        if (!prologue_matches(p, sig)) continue;
        if (++hits > 1) return nullptr;  // 2+ matches: assinatura fraca
        found = (void *)p;
    }
    return (hits == 1) ? found : nullptr;
}

// Pattern 2 (PIF updateBuildFields): clear JNI exceptions after every JNI call
// that can throw. Without this, a missed method on one OEM skin poisons the
// whole env and crashes the zygote fork.
static void jni_clear_exceptions(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        LOGW("JNI exception pendente — limpando (possível incompatibilidade de skin)");
        env->ExceptionClear();
    }
}

// Pattern 12 (PIF spoofs): runtime SDK gate via JNI Build.VERSION.SDK_INT.
// Falls back to compile-time __ANDROID_API__ if JNI lookup fails.
static int get_sdk_level(JNIEnv *env) {
    if (env == nullptr) return __ANDROID_API__;
    jclass buildClass = env->FindClass("android/os/Build");
    if (buildClass == nullptr) { jni_clear_exceptions(env); return __ANDROID_API__; }
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass == nullptr) { jni_clear_exceptions(env); env->DeleteLocalRef(buildClass); return __ANDROID_API__; }
    jfieldID fid = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (fid == nullptr) { jni_clear_exceptions(env); env->DeleteLocalRef(versionClass); env->DeleteLocalRef(buildClass); return __ANDROID_API__; }
    int sdk = env->GetStaticIntField(versionClass, fid);
    jni_clear_exceptions(env);  // safe even if no exception
    env->DeleteLocalRef(versionClass);
    env->DeleteLocalRef(buildClass);
    return sdk;
}

static bool is_bc(const char *pkg) { return pkg && strstr(pkg, "jp.co.ponos.battlecatsen"); }

static const char *TARGET_LIB = "libnative-lib.so";

// Wide JNI trampoline signature: x0..x7 (AAPCS64) sem de-rrotagem por tipo.
typedef uintptr_t (*jnifn_wide_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                  uintptr_t, uintptr_t, uintptr_t, uintptr_t);

// --- Declarações antecipadas das plans (precisam existir antes dos fakes) ---
struct HookPlan;
extern HookPlan hooks_appinit, hooks_updatedraw, hooks_apptouch, hooks_appkey;

// --- Multi-hook dispatcher (Prefix/Postfix, achado da pesquisa BepInEx/HarmonyX) ---
// A LÓGICA vive em bc_hook_logic.h (single source of truth — o
// selftest_harness inclui o MESMO header, então testa o código real, não um
// mock; ver gap achado pelo kilo). Aqui só instanciamos as estruturas e
// deletations pra API de registro e o hook_std() usarem os helpers reais.
// Dobby troca a função inteira (1 hook = 1 replacement) — Harmony permite
// VÁRIOS patches na mesma função (Prefix roda antes do original, pode
// cancelar a chamada; Postfix roda depois, pode observar/ajustar retorno).
// Isso replica o padrão POR CIMA do Dobby existente: hook_std() é o único
// ponto por onde toda chamada original passa — vira o dispatcher. 4 slots
// fixos por hook (sem alocação dinâmica, domínio pequeno e conhecido).
// Contrato igual Harmony: Prefix retorna false → PULA o original (postfix
// ainda roda, pode ver que foi pulado via *called_orig). Zero callbacks
// registrados = passthrough idêntico ao comportamento anterior.
#define HOOK_MAX_CALLBACKS 4

// Índice fixo por posição em PLANS (0=appInit,1=appUpdateDraw,2=appTouch,3=appKey)
static HookCallbacks g_hook_callbacks[BC_HOOK_NAMES_COUNT];

static uintptr_t fake_appinit(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                              uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7);
static uintptr_t fake_updatedraw(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                 uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7);
static uintptr_t fake_apptouch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                               uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7);
static uintptr_t fake_appkey(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                             uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7);

struct HookPlan {
    const char *shortname;      // "appInit"
    const char *symbol;        // "Java_jp_co_ponos_battlecats_MyActivity_appInit"
    jnifn_wide_t replacement;  // fake
    jnifn_wide_t *backup;      // onde Dobby guarda pra original
    uintptr_t rva_expected;    // offset esperado dentro da libnative-lib.so (self-test)
    const struct bc_sig *prologue; // prólogo medido (offsetsdb.h); null = sem assinatura
    int sdk_min;               // Pattern 12: SDK version gate (0 = always)
    std::atomic<unsigned> hits{0};
    void *resolved_addr = nullptr;  // endereço real hookado (install), pra unpatch reusar
};

// --- Fakes (log-only, re-forward completo) ---

// STREAMING de eventos pro companion (socket @bc_companion, comando "stream").
// Estratégia: publica aqui as MESMAS linhas que o hook loga no logcat, mas
// via socket pro companion, que faz broadcast aos clientes Termux conectados
// em modo streaming (tail -f do LogOutput.log). O game process e o daemon
// companion são processos SEPARADOS — não há memória compartilhada; o buffer
// é o kernel socket buffer (thread-safe por natureza). Usamos MSG_DONTWAIT +
// MSG_NOSIGNAL: se o buffer do socket estiver cheio (cliente lento), a linha
// é DESCARTADA — o jogo nunca bloqueia esperando I/O (requisito #3).
// Núcleo comum de streaming: monta "[HH:MM:SS] [Nível,-7:Fonte,10] " +
// corpo já pronto + envia. Extraído (achado real, kilo/devin: level/source
// hardcoded "Info"/"BCPOC" antes) pra publish_event (eventos de hook) e
// publish_log (linhas livres — warning/error, ex. build-id mismatch,
// DORMANT) compartilharem a mesma formatação sem duplicar timestamp/prefixo.
// Equivalente ao LogOutput.log do BepInEx — grava TODA linha em disco,
// sobrevive ao console fechado (gap real achado por inspeção: nada aqui
// persistia, só ia pro logcat + stream ao vivo, que somem quando o
// terminal fecha). Path em /data/local/tmp — mesmo dir que o companion já
// libera com chmod(0777) pro UID do processo do jogo escrever (achado
// documentado em companion.cpp, reusado aqui, sem permissão nova).
//
// Comportamento por padrão bate com o real (confirmado no fonte,
// DiskLogListener.cs do BepInEx): `appendLog=false` é o DEFAULT — o
// construtor abre com `FileMode.Create` (TRUNCA a cada boot), não Append.
// Aqui: fopen("w") na primeira escrita do processo — cada spawn do game
// process (1x por sessão) começa arquivo novo, mesma semântica. Se o
// arquivo estiver travado (outro processo escrevendo), tenta até 5 nomes
// (LogOutput.N.log), mesmo `fileLimit=5` do construtor real.
//
// Diferença deliberada: BepInEx usa delayedFlushing=true por padrão (timer
// de 2s). Aqui fazemos fflush por linha (equivalente ao InstantFlushing=true
// do BepInEx, opção não-default) — processo Android pode ser morto pelo
// OOM killer sem aviso, ao contrário do processo desktop; perder as
// últimas linhas antes de um crash seria pior aqui do que no PC.
#define BC_POC_LOG_PATH "/data/local/tmp/bc_poc_LogOutput.log"
#define BC_POC_LOG_FILE_LIMIT 5
static FILE *g_log_file = nullptr;
// pthread_once, não bool simples: stream_send_prefixed é chamado de várias
// threads concorrentes (hooks do jogo + logcat_bridge_thread) — achado real
// por inspeção: um bool "tried" tem TOCTOU clássico, 2 threads podem ver
// false ao mesmo tempo, ambas chamarem fopen("w") no MESMO path, cada FILE*
// com seu próprio offset zerado (fopen não herda posição), escritas
// concorrentes se sobrescrevendo em vez de acrescentar. pthread_once
// garante exatamente 1 execução real, concorrentes esperam a 1ª terminar.
static pthread_once_t g_log_file_once = PTHREAD_ONCE_INIT;

static void log_file_open() {
    char path[64];
    for (int i = 0; i < BC_POC_LOG_FILE_LIMIT; i++) {
        if (i == 0) {
            snprintf(path, sizeof(path), "%s", BC_POC_LOG_PATH);
        } else {
            snprintf(path, sizeof(path), "/data/local/tmp/bc_poc_LogOutput.%d.log", i);
        }
        g_log_file = fopen(path, "w");
        if (g_log_file != nullptr) return;
    }
    LOGW("log_file_open: fopen falhou em %d tentativas — sem persistência em disco", BC_POC_LOG_FILE_LIMIT);
}

static void log_file_write(const char *line, int len) {
    pthread_once(&g_log_file_once, log_file_open);
    if (g_log_file == nullptr) return;
    // fwrite/fflush em si não são thread-safe pra chamadas concorrentes no
    // MESMO FILE* (podem intercalar bytes de linhas diferentes) — aceitável
    // aqui: pior caso é uma linha de log espremida com outra, nunca corrompe
    // o arquivo/crasha, e o stream ao vivo (canal principal) não tem esse
    // problema (send() é atômico por datagrama para linhas desse tamanho).
    fwrite(line, 1, (size_t)len, g_log_file);
    fflush(g_log_file);
}

static void stream_send_prefixed(const char *level, const char *source,
                                 const char *body) {
    // Filtro por nível — confirmado no fonte real (freebuff, BepInEx
    // v5.4.23.5): default de [Logging.Console] e [Logging.Disk] LogLevels
    // é Fatal|Error|Message|Info|Warning, SEM Debug (ConsoleLogListener.cs,
    // Chainloader.cs). Relevante agora com o bridge de logcat (abaixo),
    // que pode receber linhas Debug/Verbose de libs nativas do processo.
    if (strcmp(level, "Debug") == 0) return;
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char line[224];
    // Formato "[Nível,-7:Fonte,10]" — real, fonte: BepInEx LogEventArgs.cs
    // (nível alinhado à esquerda em 7, fonte alinhada à direita em 10).
    // Timestamp é melhoria nossa sobre o original (BepInEx console não
    // mostra hora ao vivo, só o LogOutput.log grava — aqui vem de graça
    // porque estamos streamando por rede, não lendo arquivo depois).
    int len = snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%-7s:%10s] %s\n",
                       tmv.tm_hour, tmv.tm_min, tmv.tm_sec, level, source, body);
    if (len < 0) return;
    if (len > (int)sizeof(line) - 1) len = (int)sizeof(line) - 1;
    log_file_write(line, len);  // grava SEMPRE, mesmo sem cliente stream conectado
    int fd = g_stream_fd.load(std::memory_order_relaxed);
    if (fd < 0) return;
    // MSG_DONTWAIT: não bloqueia o jogo. MSG_NOSIGNAL: evita SIGPIPE
    // (matar o jogo) se o companion morreu/fechou o socket por baixo.
    ssize_t r = send(fd, line, (size_t)len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // canal morto (EPIPE/ECONNRESET etc.) — desliga streaming, não spamma.
        g_stream_fd.store(-1, std::memory_order_relaxed);
    }
    // EAGAIN/EWOULDBLOCK = buffer cheio → linha descartada. OK, não bloqueia.
}

// Unifica no MESMO pipeline (stream+disco) o log NATIVO do próprio processo
// do jogo — achado real (pesquisa OpenCode): BepInEx tem UnityLogSource,
// que gancha Application.logMessageReceived (evento gerenciado do Unity)
// pra capturar Debug.Log do PRÓPRIO jogo, não só de plugins. Battle Cats
// não é Unity (engine própria PONOS/Cocos2d-x-like) — não existe esse
// evento gerenciado pra ganchar. Mas o jogo ainda usa __android_log_print
// como qualquer app nativo, e isso já vai pro logcat por padrão (canal
// separado, sem módulo nenhum) — só não estava unificado com o stream/log
// do bc-poc. `logcat --pid=<próprio pid>` cobre TUDO que esse processo
// loga, jogo e módulo juntos; filtramos a própria LOG_TAG pra não duplicar
// linha que publish_log/publish_event já manda direto (senão apareceria 2x).
//
// Limitação conhecida, baixa severidade: `logcat --pid=X` continua rodando
// mesmo depois do processo X morrer (o filtro não faz o próprio logcat
// sair sozinho) — o subprocesso vira órfão (reparented pro init) quando o
// jogo é morto abruptamente (SIGKILL, sem chance de cleanup). Órfão fica
// idle (sem output novo pra imprimir, CPU desprezível) até reboot ou kill
// manual — não é crash nem vazamento de memória, só um processo parado.
// Fix completo exigiria supervisor externo (companion já root, poderia
// `pkill -9 -f "logcat.*--pid=<pid morto>"`) — fora de escopo agora.
static void *logcat_bridge_thread(void *) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "logcat -v brief --pid=%d", getpid());
    FILE *lc = popen(cmd, "r");
    if (lc == nullptr) {
        LOGW("logcat_bridge: popen falhou: %s — sem unificação de log nativo do jogo",
             strerror(errno));
        return nullptr;
    }
    char line[512];
    while (fgets(line, sizeof(line), lc) != nullptr) {
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n') line[l - 1] = '\0';
        if (line[0] == '\0') continue;
        // Formato "-v brief": "L/Tag( pid): mensagem"
        char level_c = line[0];
        char *slash = strchr(line, '/');
        if (slash == nullptr) continue;
        char *paren = strchr(slash + 1, '(');
        if (paren == nullptr) continue;
        *paren = '\0';
        const char *tag = slash + 1;
        if (strcmp(tag, LOG_TAG) == 0) continue;  // já veio via publish_log/publish_event
        char *colon = strchr(paren + 1, ':');
        const char *msg = (colon != nullptr && colon[1] == ' ') ? colon + 2 : (paren + 1);
        const char *level;
        switch (level_c) {
            case 'F': level = "Fatal";   break;
            case 'E': level = "Error";   break;
            case 'W': level = "Warning"; break;
            case 'I': level = "Info";    break;
            default:  level = "Debug";   break;  // D/V — filtrado no stream_send_prefixed
        }
        stream_send_prefixed(level, tag, msg);
    }
    pclose(lc);
    LOGI("logcat_bridge: encerrado (logcat do PID %d parou)", getpid());
    return nullptr;
}

static void start_logcat_bridge() {
    pthread_t t;
    if (pthread_create(&t, nullptr, logcat_bridge_thread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGW("start_logcat_bridge: pthread_create falhou: %s", strerror(errno));
    }
}

// Streaming de evento de hook (chamada quente, per-frame) — corpo pronto,
// sem vsnprintf (formato fixo, custo mínimo no caminho do jogo).
static void publish_event(const char *name, unsigned n,
                          uintptr_t a0, uintptr_t a1,
                          const char *level = "Info",
                          const char *source = "BCPOC") {
    char body[160];
    int blen = snprintf(body, sizeof(body), "hook #%u: %s(env=%p, jobj=%p)",
                        n, name, (void *)a0, (void *)a1);
    if (blen < 0) return;
    stream_send_prefixed(level, source, body);
}

// Streaming de linha livre (chamada fria — build-id mismatch, DORMANT) —
// aceita formato printf. Usa vsnprintf, custo aceitável fora do hot path.
static void publish_log(const char *level, const char *fmt, ...) {
    char body[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    stream_send_prefixed(level, "BCPOC", body);
}

static uintptr_t hook_std(const char *name, jnifn_wide_t orig,
                          uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                          uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                          std::atomic<unsigned> &counter) {
    unsigned n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 3 || (n % 500) == 0) {
        LOGI("hook #%u: %s(env=%p, jobj=%p)", n, name, (void *)a0, (void *)a1);
        publish_event(name, n, a0, a1);   // STREAMING pro Termux (mesma cadência)
    }
    // Dispatcher Prefix/Postfix — NÚCLEO em bc_hook_logic.h (single source
    // of truth; o selftest_harness testa hook_dispatch() do MESMO header).
    // Slot inválido/sem callback = arrays vazios, loopy não executa nada.
    int slot = hook_slot_by_name(name);
    const HookCallbacks *cb = (slot >= 0) ? &g_hook_callbacks[slot] : nullptr;

    // Medição de overhead (clock_gettime MONOTONIC) — só os laços
    // prefix/postfix, NUNCA a chamada de orig(): orig() é a função real do
    // jogo (render de frame, toque, tecla) e domina o tempo total em ordem
    // de magnitude. Núcleo hook_dispatch() mede internamente se passarmos
    // os out-s; aqui coletamos.
    long prefix_ns = 0, postfix_ns = 0;
    uintptr_t ret = hook_dispatch(name, cb, orig,
                                  a0, a1, a2, a3, a4, a5, a6, a7,
                                  &prefix_ns, &postfix_ns);
    g_hook_overhead_ns.fetch_add((uint64_t)(prefix_ns + postfix_ns), std::memory_order_relaxed);
    g_hook_overhead_count.fetch_add(1, std::memory_order_relaxed);

    return ret;
}

// Plans reais (definidas depois dos fakes)
static jnifn_wide_t orig_appinit;
static jnifn_wide_t orig_updatedraw;
static jnifn_wide_t orig_apptouch;
static jnifn_wide_t orig_appkey;

static uintptr_t fake_appinit(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                              uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    return hook_std("appInit", orig_appinit, a0, a1, a2, a3, a4, a5, a6, a7,
                    hooks_appinit.hits);
}
// appUpdateDraw hook com throttle opcional (g_throttle_every, vindo do
// bc_mods.conf tipado — substitui a antiga system property).
// Segurança: NUNCA altera se g_build_id_resolved for false — o hook só atua quando
// o build foi confirmado (identidade do jogo verificada). O throttle é só
// no controle de fluxo do próprio hook (pula a chamada original 1 a cada N
// frames), sem tocar em ponteiros ou memória do jogo. Reversível: sem linha
// throttle_every no config (ou valor 0), o comportamento volta 100% ao original.
static uintptr_t fake_updatedraw(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                  uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    int every = g_throttle_every.load(std::memory_order_relaxed);
    if (every > 0 && g_build_id_resolved.load(std::memory_order_relaxed)) {
        int n = g_frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n % every) == 0) {
            LOGI("[throttle] pulando appUpdateDraw frame #%d (1/%d)", n, every);
            return 0;  // skip: não chama orig, não desenha este frame
        }
    }
    return hook_std("appUpdateDraw", orig_updatedraw, a0, a1, a2, a3, a4, a5, a6, a7,
                    hooks_updatedraw.hits);
}
static uintptr_t fake_apptouch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                               uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    return hook_std("appTouch", orig_apptouch, a0, a1, a2, a3, a4, a5, a6, a7,
                    hooks_apptouch.hits);
}
static uintptr_t fake_appkey(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                             uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    return hook_std("appKey", orig_appkey, a0, a1, a2, a3, a4, a5, a6, a7,
                    hooks_appkey.hits);
}

// Plans finais (inducao de ordem):
//   sdk_min=0  → instala em qualquer SDK (hooks de lifecycle, seguros)
//   sdk_min=21 → exige Android 5.0+ (Dobby compat)
HookPlan hooks_appinit =
    {"appInit", "Java_jp_co_ponos_battlecats_MyActivity_appInit",
     fake_appinit, &orig_appinit, BC_OFF_APPINIT, &bc_sig_appinit, 21, {}};
HookPlan hooks_updatedraw =
    {"appUpdateDraw", "Java_jp_co_ponos_battlecats_MyActivity_appUpdateDraw",
     fake_updatedraw, &orig_updatedraw, BC_OFF_APPUPDATEDRAW, &bc_sig_appupdatedraw, 21, {}};
HookPlan hooks_apptouch =
    {"appTouch", "Java_jp_co_ponos_battlecats_MyActivity_appTouch",
     fake_apptouch, &orig_apptouch, BC_OFF_APPTOUCH, &bc_sig_apptouch, 21, {}};
HookPlan hooks_appkey =
    {"appKey", "Java_jp_co_ponos_battlecats_MyActivity_appKey",
     fake_appkey, &orig_appkey, BC_OFF_APPKEY, &bc_sig_appkey, 21, {}};

static HookPlan *PLANS[] = { &hooks_appinit, &hooks_updatedraw, &hooks_apptouch, &hooks_appkey };
static const int N_PLANS = 4;

// --- Manifests de dependência entre mods (gap 2 do BepInEx, bc_mod_graph.h) ---
// requires: Hard-only (igual BepInDependency HardDependency) — o alvo precisa
// estar DECLARADO no conjunto; conflicts: nenhum pode estar presente.
// Hoje (4 hooks independentes) ninguém declara nada — a ordem resolvida é a
// ordem de declaração (menor índice primeiro, determinístico). A estrutura
// existe pra mods reais declararem dependência sem mudar o loader.
static const struct bc_mod_manifest BC_MANIFESTS[N_PLANS] = {
    {"appInit",       {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}},
    {"appUpdateDraw", {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}},
    {"appTouch",      {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}},
    {"appKey",        {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}},
};

// Loga o status de rejeição de um mod (rótulo por causa — igual semântica do
// BepInEx DependencyErrors: erro claro por plugin, sem derrubar os irmãos).
static void bc_log_mod_reject(const char *name, enum bc_mod_status st) {
    const char *why =
        (st == BC_MOD_REJ_CONFLICT) ? "CONFLITO declarado com outro mod presente" :
        (st == BC_MOD_REJ_MISSING)  ? "REQUIRE ausente no conjunto" :
        (st == BC_MOD_REJ_CYCLE)    ? "ciclo de requires (grupo insatisfazível)" :
                                      "status desconhecido";
    LOGE("[mod-graph] %s: NÃO carregado — %s", name, why);
    publish_log("Error", "[mod-graph] %s rejeitado: %s", name, why);
}

// Resolve a ordem de carga dos PLANS[] respeitando requires/conflicts.
// Preenche out com a sequência de instalação (índices em PLANS[]) e loga
// cada rejeição. Retorna n_order. Nunca crasha (contrato do header).
static int bc_resolve_load_order(int *order, int cap) {
    struct bc_mod_graph_result res;
    int n = bc_mod_graph_sort(BC_MANIFESTS, N_PLANS, &res);
    int copied = 0;
    for (int i = 0; i < n && copied < cap; i++) {
        int idx = res.order[i];
        if (idx < 0 || idx >= N_PLANS) continue;  // defesa extra fora do header
        order[copied++] = idx;
    }
    for (int i = 0; i < N_PLANS; i++) {
        if (res.status[i] != BC_MOD_OK)
            bc_log_mod_reject(BC_MANIFESTS[i].name, res.status[i]);
    }
    LOGI("[mod-graph] ordem de carga: %d/%d mod(s) aprovado(s)", copied, N_PLANS);
    return copied;
}

// --- Self-test + resolução por alvo, chamado por try_install ANTES do DobbyHook.
// Cascata (context/battlecats-offset-db-schema.md §6):
//   1. RVA fixo do DB (exige build-id confirmado) — prólogo confere o alvo
//   2. Símbolo JNI export — prólogo confere a identidade
//   3. Scan por assinatura no segmento executável (match única obrigatória) —
//      sobrevive a deslocamento de offsets entre patches menores
// Retorna o endereço validado, ou nullptr (plano vai DORMANT).
static void *selftest_symbol(HookPlan *p) {
    // 1) caminho determinístico: build-id confirmado + RVA do DB
    if (g_build_id_resolved.load() && p->rva_expected != 0) {
        void *base = get_lib_base(TARGET_LIB);
        if (base != nullptr) {
            void *cand = (void *)((uintptr_t)base + p->rva_expected);
            if (p->prologue == nullptr || prologue_matches(cand, p->prologue)) {
                LOGD("[selftest] %s: caminho RVA direto (base=%p + %#lx)", p->shortname, base, (unsigned long)p->rva_expected);
                return cand;
            }
            LOGE("[selftest] %s: RVA %#lx aponta mas prólogo diverge — tentando símbolo/scan",
                 p->shortname, (unsigned long)p->rva_expected);
        }
    }
    // 2) símbolo (JNI export)
    void *addr = DobbySymbolResolver(TARGET_LIB, p->symbol);
    if (addr != nullptr) {
        if (p->prologue == nullptr || prologue_matches(addr, p->prologue)) {
            LOGI("[selftest] %s: símbolo resolvido e prólogo ok", p->shortname);
            return addr;
        }
        LOGE("[selftest] %s: símbolo resolve mas prólogo diverge — tentando scan", p->shortname);
    }
    // 3) assinatura: sobrevive a shift de offsets entre builds menores
    if (p->prologue != nullptr) {
        LibExecRange range{};
        if (get_lib_exec_range(TARGET_LIB, &range) != nullptr) {
            LOGD("[selftest] %s: segmento X base=%p size=%#zx — scan por assinatura", p->shortname, range.base, (size_t)range.size);
            if (void *hit = scan_exec_unique(&range, p->prologue)) {
                LOGI("[selftest] %s: achado por assinatura (offset no seg. X: %#lx)",
                     p->shortname, (unsigned long)((uintptr_t)hit - (uintptr_t)range.base));
                return hit;
            }
            LOGW("[selftest] %s: assinatura sem match única — dormant", p->shortname);
        }
    }
    return nullptr;
}

// Instala um plano, com isolamento de falha — retorna true se o hook funcionou.
static bool try_install(HookPlan *p) {
    // Gate de config (bc_mods.conf): hook desligado pelo usuário é SKIP
    // intencional — não marca g_dormant (não é falha, é escolha). Loga pra
    // ficar auditável no logcat.
    if (!hook_enabled(p->shortname)) {
        LOGI("[%s] desabilitado por config (bc_mods.conf) — pulando", p->shortname);
        publish_log("Info", "[%s] desabilitado por config — pulando", p->shortname);
        return false;
    }
    // Pattern 12: SDK gate — skip hooks that require a newer Android than
    // the runtime reports. Low-SDK devices get Dobby incompatibility, so
    // those hooks go dormant instead of crashing the game.
    if (g_sdk.load() < p->sdk_min) {
        LOGW("[%s] SDK %d < mínimo %d — pulando (dormant)", p->shortname, g_sdk.load(), p->sdk_min);
        g_dormant.store(true);
        return false;
    }
    // Pattern 8: idempotent guard — if Dobby already backed this up, the
    // hook was applied in a previous install cycle; skip re-hooking.
    if (*p->backup != nullptr) {
        LOGI("[%s] já instalado (backup != NULL) — ignorando", p->shortname);
        return true;
    }
    // Self-test + resolução: RVA (build-id ok) → símbolo → assinatura.
    void *sym = selftest_symbol(p);
    if (sym == nullptr) {
        LOGE("[%s] sem alvo confiável (RVA/símbolo/assinatura) — dormant", p->shortname);
        g_dormant.store(true);
        return false;
    }
    LOGD("[%s] alvo resolvido=%p — chamando DobbyHook", p->shortname, sym);
    // A regra de desarmamento deve ser estrita: se o hook não terminou 100%
    // *e* o trampoline pra original, o processo do jogo crasha ao primeiro
    // uso. Em qualquer falha parcial aqui, a gente desfaz tudo.
    int rc = DobbyHook(sym, (void *)p->replacement, (void **)p->backup);
    if (rc != 0) {
        LOGE("[%s] DobbyHook falhou (rc=%d) para %s — hook NÃO instalado",
             p->shortname, rc, p->symbol);
        g_dormant.store(true);
        return false;
    }
    // Guarda o endereço real hookado — unpatch_hook REUSA isto em vez de
    // re-resolver (achado real: re-resolver por prólogo depois do install
    // acha os bytes do JUMP do Dobby, não o prólogo original — scan
    // encontra match falso em outro lugar da memória e DobbyDestroy nesse
    // endereço corrompe código alheio → SIGSEGV, confirmado em device).
    p->resolved_addr = sym;
    // Auditoria pós-instalação: se Dobby ok mas o trampoline é null, tentar
    // chamar o original vai explodir. NÃO chamamos DobbyDestroy aqui: com
    // rc==0 + backup nulo o estado interno do Dobby é inconsistente
    // (hook meio-instalado), e DobbyDestroy(sym) sobre esse estado é
    // comportamento indefinido — pode restaurar bytes de uma função que nunca
    // terminou de ser patchada e corromper o i-cache. Em vez disso, anulamos
    // o trampoline do fake (evita chamar original nulo) e marcamos o plano
    // como inutilizável, deixando o Dobby manejar o próprio estado.
    if (*p->backup == nullptr) {
        // fake já trata orig==nullptr (retorna 0, não crasha) — sem ação extra.
        LOGE("[%s] backup nulo pós-hook — plano inutilizado (sem DobbyDestroy em estado inconsistente)", p->shortname);
        g_dormant.store(true);
        return false;
    }
    LOGI("hook instalado: %s", p->shortname);
    publish_log("Info", "[%s] hook instalado", p->shortname);
    return true;
}

// Remove um hook específico sem afetar os outros 3 (Harmony-style Unpatch).
// Garantia: DobbyDestroy restaura os bytes originais na função — os hooks
// irmãos mantêm seus próprios trampolines (Dobby não compartilha). Para
// o hook removido, o backup trampoline vira nullptr → fake_updatedraw etc.
// tratam orig==nullptr (retornam 0, não crasham). Chamada concorrente ao
// hook removido durante DobbyDestroy é safe side-effects-free (após destroy,
// chamadas futuras do fake retornam 0 silenciosamente).
//
// A LÓGICA de decisão (busca por shortname, idempotência, resolved_addr
// requisitado, zerar backup sem zerar resolved_addr) vive em
// bc_hook_logic.h → bc_unpatch_hook(). Este wrapper injeta o backend real
// do Dobby (DobbyDestroy) e mapeia a lógica pros campos reais do PLANS[].
// Assim o selftest_harness testa o MESMO código (sem mock).
static bool unpatch_hook(const char *shortname) {
    // Mapeia PLANS[] pros HookState que o header usa (backup + resolved_addr).
    static HookState st[BC_HOOK_NAMES_COUNT];
    HookPlan *p = nullptr;
    for (int i = 0; i < N_PLANS; i++) {
        if (strcmp(PLANS[i]->shortname, shortname) == 0) {
            p = PLANS[i];
            st[i].backup = (void *)(uintptr_t)(p->backup != nullptr ? *p->backup : nullptr);
            st[i].resolved_addr = p->resolved_addr;
            break;
        }
    }
    if (p == nullptr) {
        LOGW("[unpatch] hook desconhecido: %s", shortname);
        publish_log("Warning", "[unpatch] hook desconhecido: %s", shortname);
        return false;
    }
    // Reusa o endereço já resolvido (st.resolved_addr) — NUNCA re-resolve por
    // prólogo aqui: depois do install a função tem um JUMP no lugar do
    // prólogo, scan acha match falso e DobbyDestroy corrompe código alheio
    // (SIGSEGV confirmado em device). Isso é exatamente o que bc_unpatch_hook
    // exige (resolved != NULL) antes de chamar destroy.
    int rc = bc_unpatch_hook(st, shortname, DobbyDestroy);
    if (rc < 0) {
        if (*p->backup == nullptr) {
            LOGI("[unpatch] %s: já desativado (backup null)", p->shortname);
            publish_log("Info", "[%s] já desativado (backup null)", p->shortname);
            return true;  // idempotente
        }
        LOGE("[unpatch] %s: não desativou (sem resolved ou DobbyDestroy falhou)", p->shortname);
        publish_log("Error", "[unpatch] %s: não desativou", p->shortname);
        return false;
    }
    // Propagate state real de volta pro plano (header zerou o backup do slot).
    *p->backup = (jnifn_wide_t)(uintptr_t)st[hook_slot_by_name(shortname)].backup;
    LOGI("[unpatch] %s: hook removido (codepath original restaurado)", p->shortname);
    publish_log("Info", "[%s] hook removido", p->shortname);
    return true;
}

// Reconecta um hook removido via unpatch_hook (Harmony-style Repatch).
// Reinstala SÓ este plano — try_install() refaz self-test + DobbyHook;
// os outros hooks não são tocados. Idempotente: se já instalado (backup
// != null), faz nothing. Isso fecha o par unpatch/repatch do Harmony.
static bool repatch_hook(const char *shortname) {
    HookPlan *p = nullptr;
    for (int i = 0; i < N_PLANS; i++) {
        if (strcmp(PLANS[i]->shortname, shortname) == 0) {
            p = PLANS[i];
            break;
        }
    }
    if (p == nullptr) {
        LOGW("[repatch] hook desconhecido: %s", shortname);
        publish_log("Warning", "[repatch] hook desconhecido: %s", shortname);
        return false;
    }
    // Já está instalado (backup != null) → nada a fazer, idempotente.
    if (*p->backup != nullptr) {
        LOGI("[repatch] %s: já instalado (backup != NULL) — nada a fazer", p->shortname);
        publish_log("Info", "[%s] já instalado — nada a fazer", p->shortname);
        return true;
    }
    // reinstala SÓ este plano (try_install refaz self-test + DobbyHook).
    bool ok = try_install(p);
    if (ok) {
        publish_log("Info", "[%s] hook reinstalado (repatch)", p->shortname);
    } else {
        publish_log("Error", "[%s] repatch falhou", p->shortname);
    }
    return ok;
}

// --- list_patches: publicação do estado real dos hooks pro companion ---
// O companion não tem acesso à memória deste processo; ele é o DONO da
// verdade sobre os hooks (PLANS[]: backup trampoline + hits).
//
// DECISÃO DE DESIGN (divergência backup×config): reportamos os EIXOS
// SEPARADAMENTE, e NÃO fazemos unpatch_hook escrever g_cfg[].b. Razão:
//   - g_cfg[].b  = preferência PERSISTIDA (bc_mods.conf, toggle_mod/set_mod);
//   - *p->backup = realidade RUNTIME do Dobby (unpatch_mod/repatch_mod),
//     e p->resolved_addr distingue POR QUE um runtime não está ativo.
// São dimensões ortogonais. Se unpatch setasse g_cfg=false, o par
// quebraria: repatch_mod → try_install() consulta hook_enabled() (que lê
// g_cfg) e SKIPARIA o re-install — o "unpatch" daria um laço mortal com o
// "repatch". Logo o snapshot expõe a divergência em vez de escondê-la.
//
// 3-state de runtime (o que difere do Harmony, que falha em silêncio sem
// distinguir a causa):
//   active    = backup != NULL                      → hook vivo no Dobby
//   unpatched = backup == NULL && resolved_addr     → foi instalado, mas
//              removido por unpatch_mod (causa: ação do usuário)
//   no-target = backup == NULL && !resolved_addr    → NUNCA resolveu endereço
//              (build-id mismatch / símbolo não achou / DobbyHook falhou na
//              1ª instalação) — causa: alvo não encontrado, não ação manual
// Ex.: config=on + runtime=unpatched = usuário prefere on mas destramou;
//     config=on + runtime=no-target = prefere on mas o alvo não existia.
//
// Canal: mesma propriedade de sinal do unpatch_mod, sentido inverso —
// companion seta persist.bc_poc.patches_req com uma seq, este processo
// escreve o snapshot em /data/local/tmp/bc_patches.txt
// (mkstemp+fsync+rename, mesmo padrão do save_mods_conf) e o companion só
// serve conteúdo com a seq do pedido (nunca snapshot órfão de boot anterior —
// o arquivo persiste entre boots).
#define BC_PATCHES_PATH "/data/local/tmp/bc_patches.txt"
#define BC_PQ_PROP "persist.bc_poc.patches_req"

// Rótulo do eixo config (preferência persistida).
static const char *cfg_label(bool enabled) { return enabled ? "on" : "off"; }

// Rótulo do eixo runtime (realidade do trampoline no Dobby). 3 estados:
// active / unpatched (instalado mas removido pelo usuário) / no-target
// (nunca teve endereço resolvido — alvo não encontrado). Distingue POR QUE
// um hook não está ativo (vantagem sobre Harmony, que falha em silêncio).
static const char *rt_label(bool installed, bool resolved) {
    if (installed) return "active";
    return resolved ? "unpatched" : "no-target";
}

static void write_patches_snapshot(unsigned seq) {
    char body[1024];
    size_t used = 0;
    body[0] = '\0';
    for (int i = 0; i < N_PLANS; i++) {
        const HookPlan *p = PLANS[i];
        // Dois eixos independentes (ver decisão acima): config (preferência)
        // vs runtime (realidade). Runtime 3-state: active / unpatched /
        // no-target (usa resolved_addr pra distinguir por que está inativo).
        int w = snprintf(body + used, sizeof(body) - used, "%s|%s|%s|%u\n",
                         p->shortname, cfg_label(hook_enabled(p->shortname)),
                         rt_label(*p->backup != nullptr, p->resolved_addr != nullptr),
                         p->hits.load(std::memory_order_relaxed));
        if (w < 0 || (size_t)w >= sizeof(body) - used) break;  // nunca deve (4 linhas)
        used += (size_t)w;
    }
    char content[1100];
    int hw = snprintf(content, sizeof(content), "seq=%u\n%s", seq, body);
    if (hw < 0 || (size_t)hw >= sizeof(content)) return;

    // escrita atômica: mesmo padrão do save_mods_conf (companion) —
    // leitor nunca vê arquivo pela metade
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", BC_PATCHES_PATH);
    int tfd = mkstemp(tmp);
    if (tfd < 0) return;
    ssize_t w = write(tfd, content, (size_t)hw);
    if (w >= 0 && fsync(tfd) == 0)
        rename(tmp, BC_PATCHES_PATH);  // rename é atômico em POSIX
    else
        unlink(tmp);
    close(tfd);
}

// Instala os planos NA ORDEM do grafo de dependência — isolamento total:
// falha em um hook não derruba os outros (Pattern 11: per-hook isolation);
// mod rejeitado pelo grafo nem chega ao try_install (log claro, sem crash).
// Se todos falharem → dormant (não crasha, só loga). Se ao menos um funcionar → ACTIVE.
// --- Loader de mods .so dinâmicos (driver real, ver bc_loader.h/bc_mod_api.h) ---
// Wrappers que implementam bc_mod_api usando a infraestrutura já existente
// (g_hook_callbacks, DobbySymbolResolver, LOGx/publish_log) — nenhuma lógica
// nova aqui, só plumbing entre o contrato do mod e o que o loader já tem.
static bool mod_api_register_prefix(const char *hook, bc_prefix_fn fn) {
    return hook_register_prefix(g_hook_callbacks, hook, (HookPrefixFn)fn);
}
static bool mod_api_register_postfix(const char *hook, bc_postfix_fn fn) {
    return hook_register_postfix(g_hook_callbacks, hook, (HookPostfixFn)fn);
}
static void *mod_api_resolve_symbol(const char *sym) {
    return DobbySymbolResolver(TARGET_LIB, sym);
}
static void *mod_api_resolve_pattern(const uint8_t *pattern_bytes, const uint8_t *mask,
                                     size_t len) {
    if (pattern_bytes == nullptr || mask == nullptr || len == 0 ||
        len > BC_PATTERN_MAX_BYTES)
        return nullptr;
    bc_pattern pat = {};
    memcpy(pat.bytes, pattern_bytes, len);
    memcpy(pat.mask, mask, len);
    pat.len = len;
    void *addr = nullptr;
    bc_scan_status st = bc_pattern_scan_lib(TARGET_LIB, &pat, &addr);
    if (st != BC_SCAN_OK) return nullptr;
    return addr;
}
static void mod_api_log(bc_log_level level, const char *msg) {
    switch (level) {
        case BC_LOG_WARN:  LOGW("[mod] %s", msg); publish_log("Warning", "[mod] %s", msg); break;
        case BC_LOG_ERROR: LOGE("[mod] %s", msg); publish_log("Error", "[mod] %s", msg); break;
        default:            LOGI("[mod] %s", msg); publish_log("Info", "[mod] %s", msg); break;
    }
}

// run_entry real: sym já foi resolvido via dlsym(handle, "bc_mod_register")
// pelo bc_loader_load_one — só faz o cast e chama.
static bool mod_entry_runner(void *api, void *sym) {
    auto fn = (bc_mod_register_fn)sym;
    return fn((const bc_mod_api *)api);
}

static int qsort_strcmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// Carrega o manifest (bc_mod_manifest) de um .so já dlopen'd, se exportado.
// O manifest é um struct C estático dentro da lib (vê bc_mod_graph.h:52-55);
// dlsym("bc_mod_manifest") dá o endereço. Copiamos DENTRO do buffer local
// ANTES de dlclose, porque os ponteiros internos (name/requires_/conflicts)
// apontam pra rodata da lib — depois de dlclose apontam pra memória liberada.
// Retorna false se o .so não exporta manifest (vira nodo independente).
// Safety: lê no máximo BC_MOD_DEPS_MAX requires/conflicts (fim = NULL).
typedef struct bc_manifest_snapshot {
    char name[BC_MOD_NAME_MAX];
    char requires_[BC_MOD_DEPS_MAX][BC_MOD_NAME_MAX];
    char conflicts[BC_MOD_DEPS_MAX][BC_MOD_NAME_MAX];
    int n_req, n_con;   // contagem real (não só o null-terminado)
} bc_manifest_snapshot;

static bool discover_mod_manifest(void *handle, bc_manifest_snapshot *out) {
    if (out == nullptr) return false;
    memset(out, 0, sizeof(*out));
    const struct bc_mod_manifest *m =
        (const struct bc_mod_manifest *)dlsym(handle, "bc_mod_manifest");
    if (m == nullptr || m->name == nullptr) return false;
    snprintf(out->name, sizeof(out->name), "%s", m->name);
    for (int i = 0; i < BC_MOD_DEPS_MAX && m->requires_[i] != nullptr; i++) {
        snprintf(out->requires_[out->n_req], sizeof(out->requires_[out->n_req]),
                 "%s", m->requires_[i]);
        out->n_req++;
    }
    for (int i = 0; i < BC_MOD_DEPS_MAX && m->conflicts[i] != nullptr; i++) {
        snprintf(out->conflicts[out->n_con], sizeof(out->conflicts[out->n_con]),
                 "%s", m->conflicts[i]);
        out->n_con++;
    }
    return true;
}

// Descobre e carrega todos os .so em BC_MODS_DIR (bc_loader.h). Roda DEPOIS
// de install_all() — os hooks estáticos já estão de pé, resolve_symbol()
// funciona pros mods usarem. Isolamento de falha por arquivo: um mod que
// falha em dlopen/dlsym é pulado (logado), não derruba os outros nem o jogo
// (mesmo padrão DORMANT já usado nos hooks estáticos).
static void load_dynamic_mods() {
    DIR *dir = opendir(BC_MODS_DIR);
    if (dir == nullptr) {
        LOGI("mod loader: %s ausente ou sem acesso — sem mods dinâmicos (normal se não usa)",
             BC_MODS_DIR);
        return;
    }

    char names[64][256];
    int n_names = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr && n_names < 64) {
        if (!bc_loader_is_mod_filename(ent->d_name)) continue;
        strncpy(names[n_names], ent->d_name, sizeof(names[n_names]) - 1);
        names[n_names][sizeof(names[n_names]) - 1] = '\0';
        n_names++;
    }
    closedir(dir);

    if (n_names == 0) {
        LOGI("mod loader: %s sem .so — nada pra carregar", BC_MODS_DIR);
        return;
    }

    // Ordem de descoberta determinística (arquivo) só como desempate — a
    // ordem de CARGA real vem do grafo de dependência (bc_mod_graph_sort),
    // igual aos hooks estáticos (bc_resolve_load_order). Sem isso um mod com
    // `requires` podia carregar antes do que ele depende (gap #8, achado
    // pelo hermes na review).
    qsort(names, n_names, sizeof(names[0]), qsort_strcmp);
    if (n_names > BC_MOD_GRAPH_MAX_MODS) n_names = BC_MOD_GRAPH_MAX_MODS;

    bc_loader_ops ops = {};
    ops.dlopen = [](const char *path, int flags) -> void * { return dlopen(path, flags); };
    ops.dlsym = [](void *h, const char *sym) -> void * { return dlsym(h, sym); };
    ops.dlclose = [](void *h) -> int { return dlclose(h); };
    ops.run_entry = mod_entry_runner;

    // Fase 1: dlopen todo mundo e coleta manifest (ou fallback = nó
    // independente, nome = arquivo). Precisa estar tudo aberto ANTES de
    // ordenar, porque o manifest só existe depois do dlopen.
    void *handles[BC_MOD_GRAPH_MAX_MODS] = {};
    bc_manifest_snapshot snaps[BC_MOD_GRAPH_MAX_MODS];
    bool opened[BC_MOD_GRAPH_MAX_MODS] = {};
    int failed = 0;

    for (int i = 0; i < n_names; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", BC_MODS_DIR, names[i]);
        void *h = ops.dlopen(path, 2 /*RTLD_NOW*/);
        if (h == nullptr) {
            LOGW("mod loader: %s — dlopen falhou (corrompido/ABI incompatível?), pulando",
                 names[i]);
            failed++;
            continue;
        }
        if (ops.dlsym(h, "bc_mod_register") == nullptr) {
            LOGW("mod loader: %s — sem símbolo bc_mod_register, não é mod deste loader",
                 names[i]);
            ops.dlclose(h);
            failed++;
            continue;
        }
        handles[i] = h;
        opened[i] = true;
        if (!discover_mod_manifest(h, &snaps[i])) {
            // sem manifest exportado → nó independente (nome = arquivo,
            // sem requires/conflicts), mesmo comportamento de antes do #8.
            memset(&snaps[i], 0, sizeof(snaps[i]));
            snprintf(snaps[i].name, sizeof(snaps[i].name), "%s", names[i]);
        }
    }

    // Fase 2: monta manifests pro grafo (só os que abriram) e ordena.
    int idx_map[BC_MOD_GRAPH_MAX_MODS]; // posição no grafo -> índice em names/handles
    struct bc_mod_manifest gmods[BC_MOD_GRAPH_MAX_MODS];
    int n_gmods = 0;
    for (int i = 0; i < n_names; i++) {
        if (!opened[i]) continue;
        struct bc_mod_manifest *gm = &gmods[n_gmods];
        memset(gm, 0, sizeof(*gm));
        gm->name = snaps[i].name;
        for (int r = 0; r < snaps[i].n_req && r < BC_MOD_DEPS_MAX; r++)
            gm->requires_[r] = snaps[i].requires_[r];
        for (int c = 0; c < snaps[i].n_con && c < BC_MOD_DEPS_MAX; c++)
            gm->conflicts[c] = snaps[i].conflicts[c];
        idx_map[n_gmods] = i;
        n_gmods++;
    }

    bc_mod_api api = {};
    api.version = BC_MOD_API_VERSION;
    api.register_prefix = mod_api_register_prefix;
    api.register_postfix = mod_api_register_postfix;
    api.resolve_symbol = mod_api_resolve_symbol;
    api.log = mod_api_log;
    api.resolve_pattern = mod_api_resolve_pattern;

    int ok = 0, inactive = 0;
    if (n_gmods > 0) {
        struct bc_mod_graph_result res;
        int n_order = bc_mod_graph_sort(gmods, n_gmods, &res);

        // Fase 3: carrega na ordem do grafo (só os aprovados).
        for (int k = 0; k < n_order; k++) {
            int gi = res.order[k];
            int i = idx_map[gi];
            void *sym = ops.dlsym(handles[i], "bc_mod_register");
            bool active = mod_entry_runner(&api, sym);
            if (active) {
                LOGI("mod loader: %s carregado e ativo", names[i]);
                ok++;
            } else {
                LOGI("mod loader: %s carregado mas inativo (entry retornou false)", names[i]);
                ops.dlclose(handles[i]);
                inactive++;
            }
            opened[i] = false; // marcado como tratado (evita dlclose duplo abaixo)
        }

        // Mods rejeitados pelo grafo (conflict/missing/cycle): nunca chama o
        // entry, fecha o handle sem tentar carregar (isolamento, igual a um
        // dlopen que falhou — não derruba os outros nem o jogo).
        for (int gi = 0; gi < n_gmods; gi++) {
            int i = idx_map[gi];
            if (!opened[i]) continue; // já tratado (entrou na ordem)
            const char *why = (res.status[gi] == BC_MOD_REJ_CONFLICT) ? "conflict"
                             : (res.status[gi] == BC_MOD_REJ_MISSING) ? "requires ausente"
                             : (res.status[gi] == BC_MOD_REJ_CYCLE)   ? "ciclo de dependência"
                                                                       : "rejeitado";
            LOGW("mod loader: %s — rejeitado pelo grafo (%s), pulando", names[i], why);
            ops.dlclose(handles[i]);
            failed++;
            opened[i] = false;
        }
    }

    publish_log("Message", "mod loader: %d ok, %d inativo, %d falhou (de %d .so em %s)",
                ok, inactive, failed, n_names, BC_MODS_DIR);
}

static void install_all() {
    int order[N_PLANS];
    int n = bc_resolve_load_order(order, N_PLANS);
    int ok = 0;
    for (int i = 0; i < n; i++) {
        if (try_install(PLANS[order[i]])) ok++;
    }
    LOGI("instalação final: %d/%d hooks funcionais", ok, N_PLANS);
    if (ok == 0) {
        LOGW("nenhum hook instalado — modulo em DORMANT (log-only, sem crash)");
        publish_log("Warning", "nenhum hook instalado — modulo em DORMANT (log-only, sem crash)");
    }
}

// Espera a lib nativa aparecer (poll em dl_iterate_phdr, deadline real ~timeout_ms).
// Usa clock monotônico (não wall-clock), então o limite não estoura em
// suspensão/scheduler lag — o "max ~5s" é garantido, não aproximado.
struct LibPollCtx { const char *libname; std::atomic<int> found{0}; };
static bool wait_lib_loaded(const char *libname, int timeout_ms) {
    LibPollCtx ctx{libname};
    timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        ctx.found.store(0, std::memory_order_relaxed);
        dl_iterate_phdr(+[](struct dl_phdr_info *info, size_t, void *d) -> int {
            auto *c = static_cast<LibPollCtx *>(d);
            if (info->dlpi_name && strstr(info->dlpi_name, c->libname))
                c->found.store(1, std::memory_order_relaxed);
            return 0;
        }, &ctx);
        if (ctx.found.load(std::memory_order_relaxed)) return true;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L +
                          (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed_ms >= timeout_ms) return false;
        usleep(8000);
    }
}

// Watch callback: throttle_every mudou → recalcula g_throttle_every (mesmo
// derivado que apply_mods_config usa no boot). Exemplo real de uso do padrão
// watch-per-key (BepInEx ConfigEntry.SettingChanged port). Dispara quando a
// chave "throttle_every" muda entre reloads, mantendo o runtime consistente
// sem precisar de restart.
static void watch_throttle(const char *key, const struct bc_mod_entry *old_val,
                            const struct bc_mod_entry *new_val) {
    (void)key; (void)old_val;
    int v = (new_val->present && new_val->i >= 1) ? (int)new_val->i : 0;
    g_throttle_every.store(v, std::memory_order_relaxed);
    if (v > 0)
        LOGI("[watch] %s mudou → pula 1 frame a cada %d", key, v);
}

// Thread de espera + instalação
static void *event_thread(void *) {
    if (!wait_lib_loaded(TARGET_LIB, 5000)) {
        LOGE("libnative-lib não apareceu em 5s");
        return nullptr;
    }
    LOGI("libnative-lib.so detected — instalando hooks");
    // Banner estilo BepInEx (achado freebuff: Chainloader imprime "BepInEx
    // X.Y.Z - <game>" em Message no início do carregamento) — nosso
    // equivalente: banner do módulo antes de qualquer instalação.
    publish_log("Message", "BC POC Zygisk %d hooks disponíveis — iniciando instalação", N_PLANS);
    start_logcat_bridge();  // unifica log nativo do jogo no mesmo canal (1x por sessão)
    // Opção A (§8): valida o build ANTES de qualquer instalação.
    g_build_id_resolved.store(verify_build_id(TARGET_LIB), std::memory_order_relaxed);
    install_all();
    LOGI("state: %s", g_dormant.load() ? "DORMANT" : "ACTIVE");
    publish_log("Message", "instalação concluída — state: %s", g_dormant.load() ? "DORMANT" : "ACTIVE");
    // Mods dinâmicos DEPOIS dos hooks estáticos — resolve_symbol() já funciona.
    load_dynamic_mods();
    // Registra callbacks watch-per-key (BepInEx SettingChanged port).
    // Exemplo: throttle_every mudou → recalcula g_throttle_every em runtime.
    bc_mod_watch_register(g_watch_table, BC_SCHEMA_N, &g_watch_count,
                          "throttle_every", watch_throttle);
    // relatório depois de 5s de atividade
    sleep(5);
    for (int i = 0; i < N_PLANS; i++) {
        unsigned n = PLANS[i]->hits.load(std::memory_order_relaxed);
        if (n > 0) LOGI("%s: %u interceptações", PLANS[i]->shortname, n);
    }
    // Hot-reload de config: polling de system property (companion seta
    // persist.bc_poc.reload_config=1 via comando 'reload_config'). Se
    // detectado, recarrega bc_mods.conf e zera a property. Polling a cada
    // 1s — custo aceitável fora do hot path de hooks.
    for (;;) {
        sleep(1);
        char buf[PROP_VALUE_MAX] = {0};
        if (__system_property_get("persist.bc_poc.reload_config", buf) > 0 &&
            strcmp(buf, "1") == 0) {
            struct bc_mod_entry before[8];
            memcpy(before, g_cfg, sizeof(before));
            load_mods_config();
            __system_property_set("persist.bc_poc.reload_config", "0");
            // Guard de no-op (BepInEx SettingChanged só dispara em delta real):
            // compara antes/depois, só loga o que mudou de fato.
            int changed = 0;
            // Watch-per-key (BepInEx SettingChanged port): para cada chave que
            // mudou, dispara callback registrado. Usa bc_mods_entry_equal (valor
            // + present) — mais preciso que a comparação manual acima que
            // ignora present. bc_mod_watch_find/busca fatoradas em bc_mods_conf.h.
            // (BepInEx ConfigFile.cs:596-510: try/catch por callback; em C não
            // há exceções → callbacks internos devem ser não-panicking.)
            for (int i = 0; i < BC_SCHEMA_N; i++) {
                bool same;
                switch (BC_SCHEMA[i].type) {
                    case BC_MOD_BOOL: same = (before[i].b == g_cfg[i].b); break;
                    case BC_MOD_INT:  same = (before[i].i == g_cfg[i].i); break;
                    default:          same = (strcmp(before[i].s, g_cfg[i].s) == 0); break;
                }
                if (!same) {
                    changed++;
                    publish_log("Info", "reload_config: %s mudou", g_cfg[i].name);
                    bc_mod_watch_fn fn = bc_mod_watch_find(g_watch_table, g_watch_count,
                                                            g_cfg[i].name);
                    if (fn != nullptr)
                        fn(g_cfg[i].name, &before[i], &g_cfg[i]);
                }
            }
            if (changed == 0) {
                LOGI("reload_config: sem mudança — nada aplicado");
            } else {
                LOGI("reload_config: %d chave(s) mudou/mudaram", changed);
                publish_log("Info", "config recarregado (reload_config, %d mudança(s))", changed);
            }
        }
        // unpatch_mod <nome>: companion sinaliza via property (não tem acesso
        // à memória do game process pra chamar DobbyDestroy direto), game
        // process poll aqui e executa o unpatch real.
        char unbuf[PROP_VALUE_MAX] = {0};
        if (__system_property_get("persist.bc_poc.unpatch_target", unbuf) > 0 &&
            unbuf[0] != '\0') {
            LOGI("unpatch_mod signal detectado: %s", unbuf);
            unpatch_hook(unbuf);
            __system_property_set("persist.bc_poc.unpatch_target", "");
        }
        // repatch_mod <nome>: mesmo padrão cross-process — companion sinaliza
        // via property, game process poll e re-instala SÓ o hook nomeado.
        char rebuf[PROP_VALUE_MAX] = {0};
        if (__system_property_get("persist.bc_poc.repatch_target", rebuf) > 0 &&
            rebuf[0] != '\0') {
            LOGI("repatch_mod signal detectado: %s", rebuf);
            repatch_hook(rebuf);
            __system_property_set("persist.bc_poc.repatch_target", "");
        }
        // reload_mods: companion sinaliza que um push_mod escreveu (ou removeu)
        // .so em BC_MODS_DIR. O game process re-executa o MESMO
        // load_dynamic_mods() (bc_loader.h + bc_mod_graph.h, fase de
        // descoberta→grafo→carga) — NÃO existe um caminho de load separado pro
        // push; o push só grava o arquivo, o loader canônico enxerga a mudança
        // assim que roda de novo. Cross-process: companion seta a property,
        // este processo faz o poll (mesmo padrão do unpatch/repatch/reload).
        char rmbuf[PROP_VALUE_MAX] = {0};
        if (__system_property_get("persist.bc_poc.reload_mods", rmbuf) > 0 &&
            strcmp(rmbuf, "1") == 0) {
            LOGI("reload_mods signal detectado — re-executando load_dynamic_mods()");
            load_dynamic_mods();
            __system_property_set("persist.bc_poc.reload_mods", "0");
        }
        // Exportar overhead do dispatcher via property (companion lê pro comando hook_overhead)
        uint64_t total_ns = g_hook_overhead_ns.load(std::memory_order_relaxed);
        uint64_t count = g_hook_overhead_count.load(std::memory_order_relaxed);
        char overhead_buf[64];
        if (count > 0) {
            double avg_us = (double)total_ns / count / 1000.0;
            snprintf(overhead_buf, sizeof(overhead_buf), "%.2f", avg_us);
        } else {
            snprintf(overhead_buf, sizeof(overhead_buf), "0");
        }
        __system_property_set("persist.bc_poc.hook_overhead_us", overhead_buf);
        // list_patches: companion pediu snapshot do estado dos hooks
        // (persist.bc_poc.patches_req = seq do pedido). Responde escrevendo
        // o snapshot com a MESMA seq — o companion só serve arquivo com a
        // seq do pedido atual (nunca snapshot órfão de boot anterior).
        char pqbuf[PROP_VALUE_MAX] = {0};
        if (__system_property_get(BC_PQ_PROP, pqbuf) > 0 && pqbuf[0] != '\0') {
            write_patches_snapshot((unsigned)strtoul(pqbuf, nullptr, 10));
            __system_property_set(BC_PQ_PROP, "");
        }
    }
    return nullptr;
}

// Adapter: bc_generic_hook_install_all espera fn pointer puro (symbol, count),
// publish_log é variádica — junta os dois sem mudar a assinatura de publish_log.
static void generic_hook_log_cb(const char *symbol, uint64_t call_count) {
    LOGI("[generico] %s chamado (%llu)", symbol, (unsigned long long)call_count);
    publish_log("Info", "[generico] %s chamado (%llu)", symbol, (unsigned long long)call_count);
}

// Thread genérica de espera + instalação — equivalente ao event_thread do
// Battle Cats, mas sem nome de lib fixo pra esperar (não sabemos qual é a
// lib nativa do jogo genérico). bc_wait_engine_detect faz poll da cascata
// inteira (Cocos2d-x + fallback genérico) até achar ou estourar timeout.
// 8000ms/200ms: janela um pouco maior que os 5000ms fixos do Battle Cats
// (lib de nome conhecido responde mais rápido a um simples name-match;
// aqui cada poll faz enumeração de símbolo em todas as libs carregadas,
// mais caro por iteração, por isso o intervalo de 200ms em vez de 8ms).
static void *generic_event_thread(void *arg) {
    const char *pkg = (const char *)arg;
    // ACHADO REAL (teste ao vivo no device, 2026-09-17): app com chamada
    // JNI única logo após System.loadLibrary() (padrão comum de init) pode
    // rodar ANTES do poll instalar o hook — DobbyInstrument só intercepta
    // chamada FUTURA a partir do momento em que instala, não retroage.
    // Intervalo menor (50ms em vez de 200ms) reduz a janela de corrida,
    // mas não elimina: se a call acontecer no mesmo instante do
    // System.loadLibrary(), nenhum poll síncrono pega a tempo. Fix de
    // verdade (hookar JNI_OnLoad/dlopen) é fora de escopo — custo real
    // do scan a cada 50ms é aceitável só durante a janela de 8s, não
    // indefinidamente.
    bc_engine_signal sig = bc_wait_engine_detect(8000, 50);
    if (sig == BC_ENGINE_UNKNOWN) {
        LOGI("%s: nenhum símbolo Java_* achado em 8s — dormant (RegisterNatives blind spot ou app não-nativo)", pkg);
        publish_log("Info", "generalização: %s sem engine/símbolo detectado em 8s — nada instalado", pkg);
        return nullptr;
    }
    LOGI("%s: engine nativo detectado (sinal=%d) — instalando hook de log", pkg, (int)sig);
    int n = bc_generic_hook_install_all(generic_hook_log_cb);
    LOGI("%s: %d hook(s) de log instalado(s)", pkg, n);
    publish_log("Info", "generalização: %s — %d símbolo(s) Java_* com hook de log instalado(s)", pkg, n);
    return nullptr;
}

class BCModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("módulo carregado — %s", BC_LOADER_VERSION);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const char *pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (pkg == nullptr) {
            api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        be_bc = is_bc(pkg);
        if (!be_bc) {
            // Generalização Cocos2d-x/C++ nativo (pedido do usuário
            // 2026-09-16): só escaneia/atua em pacote explicitamente na
            // allowlist — detectar em TODO app do device custaria latência
            // de boot pra apps que não interessam. bc_generic_allowlist.h.
            //
            // ACHADO REAL (revisão freebuff, testado ao vivo no device):
            // NÃO detecta aqui — preAppSpecialize roda ANTES do processo
            // ser especializado, nenhuma lib do app está mapeada ainda
            // (scan ao vivo em zygote64 confirmou 0 símbolos Java_* nesse
            // estágio). Só marca o candidato pela allowlist; a detecção de
            // verdade (com poll+timeout, já que não sabemos o nome da lib
            // como no caminho Battle Cats) acontece em postAppSpecialize.
            char pkg_copy[256];
            snprintf(pkg_copy, sizeof(pkg_copy), "%s", pkg);
            env->ReleaseStringUTFChars(args->nice_name, pkg);
            be_generic_candidate = bc_generic_allowlist_contains(pkg_copy);
            if (be_generic_candidate) {
                snprintf(be_generic_pkg, sizeof(be_generic_pkg), "%s", pkg_copy);
                LOGI("%s na allowlist — detecção de engine adiada pra postAppSpecialize", pkg_copy);
                // BUG REAL achado por revisão (hermes): sem isso, publish_log()
                // chamado pelo hook genérico (generic_hook_log_cb) nunca tem
                // g_stream_fd setado — o log só ia pro disco/logcat, nunca pro
                // Termux, porque só o caminho be_bc chamava connectCompanion().
                // Mesma restrição de SELinux do caminho BC: só funciona aqui,
                // em preAppSpecialize.
                int companion_fd = api->connectCompanion();
                if (companion_fd >= 0) {
                    g_stream_fd.store(companion_fd, std::memory_order_relaxed);
                } else {
                    LOGE("connectCompanion() falhou (caminho genérico) — companion não vai subir");
                }
            } else {
                api->setOption(Option::DLCLOSE_MODULE_LIBRARY);
            }
            return;
        }
        env->ReleaseStringUTFChars(args->nice_name, pkg);
        LOGI("preAppSpecialize de jp.co.ponos.battlecatsen — entrando");
        // (throttle migrado pra bc_mods.conf tipado — throttle_every=N;
        //  a leitura agora é feita em load_mods_config() no postAppSpecialize.
        //  A property persist.bc_poc.mod_enabled deixou de ser lida.)
        // connectCompanion() só funciona aqui (pre-specialize) — depois disso
        // o Zygisk é descarregado do processo por restrição de SELinux e a
        // chamada falha sempre. Achado por revisão (hermes), doc zygisk.hpp:213.
        // Sem essa chamada, companion_handler() nunca roda e o socket
        // @bc_companion pro Termux nunca existe.
        int companion_fd = api->connectCompanion();
        if (companion_fd >= 0) {
            LOGI("companion conectado (fd=%d) — socket @bc_companion deve estar ativo", companion_fd);
            // NÃO é leak: este fd é o canal STREAMING de eventos pro companion
            // (comando "stream" no Termux), usado por publish_event() com
            // MSG_DONTWAIT (nunca bloqueia o jogo) durante toda a vida do
            // processo do app. Fechar cedo derruba o streaming inteiro — fica
            // aberto até o processo do app terminar (SO recupera o fd então).
            // Achado histórico: fechar logo após connectCompanion() também
            // impedia o companion_handler de rodar (corrida com o dispatch do
            // daemon zygiskd) — outro motivo pra não fechar aqui.
            g_stream_fd.store(companion_fd, std::memory_order_relaxed);
        } else {
            LOGE("connectCompanion() falhou — companion não vai subir");
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (be_generic_candidate) {
            // Detecção de verdade acontece AQUI (postAppSpecialize), não em
            // preAppSpecialize — achado freebuff acima. Ainda assim as libs
            // do app podem não ter terminado de carregar neste ponto exato
            // (mesmo motivo por que o caminho Battle Cats usa event_thread +
            // wait_lib_loaded em vez de instalar direto aqui); como não
            // sabemos o nome da lib alvo, a espera é por poll da cascata
            // inteira em vez de por nome — bc_wait_engine_detect.
            pthread_t t;
            if (pthread_create(&t, nullptr, generic_event_thread, be_generic_pkg) != 0) {
                LOGE("pthread_create (generic_event_thread) falhou");
                return;
            }
            pthread_detach(t);
            return;
        }
        if (!be_bc) return;
        LOGI("postAppSpecialize — subindo thread de hooks");
        // Config de hooks (bc_mods.conf): lido AQUI, síncrono e antes da
        // event_thread — a leitura tem que terminar antes de install_all()
        // rodar, senão a decisão de gate usa tabela vazia (race de boot).
        load_mods_config();
        // Pattern 12: populando g_sdk via JNI Build.VERSION.SDK_INT ANTES de
        // subir a thread de hooks. Se o store fosse depois do pthread_create,
        // event_thread → try_install leria g_sdk==0 e mandaria todos os hooks
        // pra DORMANT por SDK gate (race determinística no boot).
        g_sdk.store(get_sdk_level(env), std::memory_order_relaxed);
        LOGI("SDK level: %d", g_sdk.load());
        pthread_t t;
        if (pthread_create(&t, nullptr, event_thread, nullptr) != 0) {
            LOGE("pthread_create falhou");
            return;
        }
        pthread_detach(t);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool be_bc = false;
    bool be_generic_candidate = false;
    char be_generic_pkg[256] = {};
};

REGISTER_ZYGISK_MODULE(BCModule)
