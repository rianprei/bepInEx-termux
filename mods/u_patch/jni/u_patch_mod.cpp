// u_patch — motor declarativo (FASE F4): lê todo *.patch + <id>.conf de
// /data/local/tmp/mods/<pkg>/ e aplica as regras C4 (return/mul/static).
// Regra que não resolve vira log e o jogo segue. Nada de offset fixo: tudo
// sai da API il2cpp exportada. Log mínimo próprio (F2 dá mod_common.h e a
// gente troca — até lá, logcat + log.txt C1 aqui mesmo).
#include <android/log.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "dobby.h"
#include "../../common/il2cpp_min.h"
#include "u_patch_parse.h"
#include "u_patch_arm64.h"

#define UP_TAG "u_patch"
#define UP_MODS_DIR_FMT "/data/local/tmp/mods/%s"
#define UP_LOG_FMT "/data/data/%s/files/bepinex/log.txt"
#define UP_RULE_MAX 128   // regras distintas lembradas (dedupe reaplicação)
#define UP_STATIC_MAX 32  // campos static fixados (reaplica a cada 2s)

static char up_pkg[128];
static char up_dir[320];

// Log mínimo C1: logcat + append em log.txt (HH:MM:SS [u_patch] msg).
// Teto de 256KB: rename pra log.txt.1 (não 'w', que zera tudo) e segue com
// O_APPEND num write() único por linha — atômico entre processos até PIPE_BUF.
// Só logs de u_patch vão rotacionar o arquivo (outros mods escrevem lá também;
// cada um decide o próprio teto).
static void up_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, UP_TAG, "%s", msg);
    if (!up_pkg[0]) return;
    char path[384];
    snprintf(path, sizeof(path), UP_LOG_FMT, up_pkg);
    // mkdir -p files/bepinex (jogo escreve no próprio diretório, ok).
    char dir[384];
    snprintf(dir, sizeof(dir), UP_LOG_FMT, up_pkg);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; }
    char *mk = dir + 1;
    for (char *p = mk; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
    }
    mkdir(dir, 0755);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 256 * 1024) {
        char old[392];
        snprintf(old, sizeof(old), "%s.1", path);
        rename(path, old);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char line[640];
    int len = snprintf(line, sizeof(line), "%02d:%02d:%02d [%s] %s\n",
                       tmv.tm_hour, tmv.tm_min, tmv.tm_sec, UP_TAG, msg);
    if (len > 0) write(fd, line, (size_t)len);
    close(fd);
}

// Pacote (C1): env do loader primeiro; fallback cmdline com RETRY real —
// no constructor ainda é zygote64 (achado device), então o worker re-tenta
// a cada 1s até o cmdline virar o do app. Sem pthread_once (o fallback
// precisa re-rodar); sem corrida: o constructor roda ANTES do
// pthread_create (happens-before), depois só o worker chama.
static bool up_read_pkg() {
    const char *env = getenv("BEPINEX_PKG");
    if (env && env[0] && !up_is_zygote(env)) {
        snprintf(up_pkg, sizeof(up_pkg), "%s", env);
        snprintf(up_dir, sizeof(up_dir), UP_MODS_DIR_FMT, up_pkg);
        return true;
    }
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (!f) return false;
    char cmd[128] = {};
    size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
    fclose(f);
    if (n == 0 || up_is_zygote(cmd)) return false;
    snprintf(up_pkg, sizeof(up_pkg), "%s", cmd);
    snprintf(up_dir, sizeof(up_dir), UP_MODS_DIR_FMT, up_pkg);
    return true;
}

static bool up_ends_with(const char *name, const char *suf) {
    size_t nl = strlen(name), sl = strlen(suf);
    return nl >= sl && strcmp(name + nl - sl, suf) == 0;
}

// Lê arquivo inteiro (até 64KB). Retorna malloc'd, *len = tamanho.
static char *up_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "r");
    if (!f) return nullptr;
    char *buf = (char *)malloc(65536);
    if (!buf) { fclose(f); return nullptr; }
    size_t n = fread(buf, 1, 65535, f);
    fclose(f);
    buf[n] = '\0';
    if (len) *len = n;
    return buf;
}

// Escreve palavras no código do método: mprotect RWX -> escreve ->
// flush -> volta pra RX. false = mprotect falhou (não escreve).
static bool up_patch_code(void *addr, const uint32_t *words, int nwords) {
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + (size_t)nwords * 4;
    uintptr_t p0 = start & ~((uintptr_t)ps - 1);
    uintptr_t p1 = (end - 1) & ~((uintptr_t)ps - 1);
    if (mprotect((void *)p0, (size_t)(p1 - p0 + ps), PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    memcpy((void *)start, words, (size_t)nwords * 4);
    __builtin___clear_cache((char *)start, (char *)end);
    if (mprotect((void *)p0, (size_t)(p1 - p0 + ps), PROT_READ | PROT_EXEC) != 0)
        __android_log_print(ANDROID_LOG_ERROR, UP_TAG, "mprotect RX de volta falhou em %p (%s) — página ficou RWX", (void *)p0, strerror(errno));
    return true;
}

// Valor final da regra: número/true/false direto, ou $key do <id>.conf.
static bool up_resolve_value(const char *raw, const char *conf, char *out, size_t outsz) {
    if (raw[0] != '$') { snprintf(out, outsz, "%s", raw); return true; }
    if (!conf || !up_conf_get(conf, raw + 1, out, outsz)) return false;
    return true;
}

static bool up_parse_bool(const char *s, int *out) {
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) { *out = 1; return true; }
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) { *out = 0; return true; }
    return false;
}

// int32 com sinal ou uint32 (0x hex ok). false = fora da faixa/lixo.
static bool up_parse_int32(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (!*s) return false;
    uint64_t v = 0;
    for (; *s; s++) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return false;
        v = v * (uint64_t)base + (uint64_t)d;
        if (v > 0xFFFFFFFFull) return false;
    }
    if (neg) {
        if (v > 0x80000000ull) return false;
        *out = (uint32_t)(0u - (uint32_t)v);
    } else {
        *out = (uint32_t)v;
    }
    return true;
}

struct up_applied_t {
    char sig[224];
    uint8_t state;  // 1 = falhou 1x (tenta de novo em silêncio), 2 = aplicada
};
static up_applied_t up_applied[UP_RULE_MAX];
static int up_applied_n;

struct up_static_t {
    void *klass;
    void *field;
    char cls[128];
    char member[128];
    uint8_t bytes[8];
    size_t size;
    char sig[224];
};
static up_static_t up_statics[UP_STATIC_MAX];
static int up_statics_n;

// Página RX com os thunks mul + slots em .bss.
static uint32_t *up_thunk_page;
static int up_thunk_used;
// Layout do slot de dados TEM que bater com UP_SLOT_* (encoders do thunk
// somam os offsets na mão). union pros bits float/int no mesmo offset.
struct up_slot_t {
    void *orig;              // +0  = UP_SLOT_ORIG
    union {
        int64_t ivalue;      // +8  = UP_SLOT_VALUE (int)
        float fvalue;        // +8  (float, 32 baixos)
    };
    uint64_t lr_save;        // +16 = UP_SLOT_LR
    uint64_t lock;           // +24 = UP_SLOT_LOCK
};
static_assert(offsetof(up_slot_t, orig) == UP_SLOT_ORIG, "slot layout");
static_assert(offsetof(up_slot_t, ivalue) == UP_SLOT_VALUE, "slot layout");
static_assert(offsetof(up_slot_t, fvalue) == UP_SLOT_VALUE, "slot layout");
static_assert(offsetof(up_slot_t, lr_save) == UP_SLOT_LR, "slot layout");
static_assert(offsetof(up_slot_t, lock) == UP_SLOT_LOCK, "slot layout");
static_assert(sizeof(up_slot_t) == UP_SLOT_SIZE, "slot layout");
static up_slot_t up_slots[UP_MUL_MAX];

// Pool field: um slot por método hookado (só o orig; valor vai embutido no
// thunk). Página RX própria, mesmo esquema do mul.
struct up_fslot_t {
    void *orig;  // +0 = UP_SLOT_ORIG
};
static_assert(offsetof(up_fslot_t, orig) == UP_SLOT_ORIG, "fslot layout");
static up_fslot_t up_fslots[UP_FIELD_MAX];
static uint32_t *up_field_page;
static int up_field_used;

static int up_find_applied(const char *sig) {
    for (int i = 0; i < up_applied_n; i++)
        if (strcmp(up_applied[i].sig, sig) == 0) return i;
    return -1;
}

static void up_mark(const char *sig, uint8_t state) {
    int i = up_find_applied(sig);
    if (i >= 0) { up_applied[i].state = state; return; }
    if (up_applied_n >= UP_RULE_MAX) return;
    snprintf(up_applied[up_applied_n].sig, sizeof(up_applied[0].sig), "%s", sig);
    up_applied[up_applied_n].state = state;
    up_applied_n++;
}

// Log de falha só na 1ª vez por regra (parcial 11): a mesma regra é
// reavaliada a cada 2s; sem isso ela enchia o log a cada passada.
static void up_log_first(const char *sig, const char *fmt, ...) {
    int i = up_find_applied(sig);
    if (i >= 0 && up_applied[i].state != 0) return;  // já logou essa regra
    va_list ap;
    char msg[448];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    up_log("%s", msg);
    up_mark(sig, 1);  // marca como "falhou/logou" — sucesso sobrescreve com 2
}

static void up_sig(char *out, size_t n, const char *id, const up_rule_t *r) {
    // field carrega o método no 8º campo (auto = ""): duas regras no mesmo
    // campo com métodos diferentes não podem colidir no dedupe. T1 casa
    // `id|...|cls|campo|...` + "field aplicado" — ordem mantida.
    if (r->kind == UP_FIELD)
        snprintf(out, n, "%s|%d|%s|%s|%d|%d|%s|%s", id, (int)r->kind, r->cls,
                 r->member, r->nargs, (int)r->type, r->value, r->fmethod);
    else
        snprintf(out, n, "%s|%d|%s|%s|%d|%d|%s", id, (int)r->kind, r->cls,
                 r->member, r->nargs, (int)r->type, r->value);
}

static bool up_apply_return(const Il2Cpp &il, void *klass, const up_rule_t *r, const char *valstr, const char *sig) {
    void *m = il.class_get_method_from_name(klass, r->member, r->nargs);
    if (!m) { up_log_first(sig, "método %s::%s(%d) não achado", r->cls, r->member, r->nargs); return false; }
    void *code = *(void **)m;  // methodPointer = 1º campo (il2cpp >= 2019.3, il2cpp-class.h; antes tinha um struct MethodInfo prefixado).
    if (!code) { up_log_first(sig, "método sem código (genérico/abstrato?)"); return false; }
    if (*(uint32_t *)code == UP_RET) { up_log_first(sig, "método já é só ret, pulando"); return false; }
    // Genérico compartilhado (methodPointer = código de um método genérico
    // outro qualquer): patchar corromperia chamadas do método doador.
    // Heurística barata (sem reflexão de método): movz x0,#imm16 seguido de
    // ret não vem de código real de método — é o padrão de stub compartilhado.
    if (*(uint32_t *)((uintptr_t)code + 4) == UP_RET) {
        up_log("%s: aviso: código parece stub compartilhado (2ª palavra ret) — se outro método sumir, essa é a causa", sig);
    }
    uint32_t words[4];
    int n = 0;
    const char *tname = "?";
    if (r->type == UP_BOOL) {
        tname = "bool";
        int b = 0;
        if (!up_parse_bool(valstr, &b)) { up_log_first(sig, "bool inválido '%s'", valstr); return false; }
        n = up_emit_return_bool(words, b);
    } else if (r->type == UP_INT) {
        tname = "int";
        uint32_t v = 0;
        if (!up_parse_int32(valstr, &v)) { up_log_first(sig, "int inválido '%s'", valstr); return false; }
        n = up_emit_return_int(words, v);
    } else {
        tname = "float";
        char *end = nullptr;
        float f = strtof(valstr, &end);
        if (!end || *end || f != f) { up_log_first(sig, "float inválido '%s'", valstr); return false; }
        uint32_t bits = 0;
        memcpy(&bits, &f, 4);
        n = up_emit_return_float(words, bits);
    }
    // Guard de método curto: escrever além do ret/B vazaria pro método
    // seguinte. Recusa com log em vez de corromper código alheio.
    if (!up_method_fits((const uint32_t *)code, n)) {
        up_log_first(sig, "metodo curto demais pra return %s (%d bytes), pulando", tname, n * 4);
        return false;
    }
    if (!up_patch_code(code, words, n)) { up_log_first(sig, "mprotect falhou em %p", code); return false; }
    up_log("%s: return aplicado @%p (%d bytes)", sig, code, n * 4);
    return true;
}

static bool up_apply_mul(const Il2Cpp &il, void *klass, const up_rule_t *r, const char *valstr, const char *sig) {
    if (!up_thunk_page) { up_log_first(sig, "sem página de thunk (mmap falhou)"); return false; }
    if (up_thunk_used >= UP_MUL_MAX) { up_log_first(sig, "teto de %d thunks mul, ignorada", UP_MUL_MAX); return false; }
    void *m = il.class_get_method_from_name(klass, r->member, r->nargs);
    if (!m) { up_log_first(sig, "método %s::%s(%d) não achado", r->cls, r->member, r->nargs); return false; }
    void *code = *(void **)m;
    if (!code) { up_log_first(sig, "método sem código"); return false; }
    // Dobby troca o prólogo por salto (~16 bytes): método menor não tem onde.
    if (!up_method_fits((const uint32_t *)code, 4)) {
        up_log_first(sig, "metodo curto demais pra hook Dobby @%p, pulando", code);
        return false;
    }
    bool is_float = (r->type == UP_FLOAT);
    up_slot_t *slot = &up_slots[up_thunk_used];
    memset(slot, 0, sizeof(*slot));
    if (is_float) {
        char *end = nullptr;
        double d = strtod(valstr, &end);
        if (!end || *end) { up_log_first(sig, "fator float inválido '%s'", valstr); return false; }
        slot->fvalue = (float)d;
    } else {
        char *end = nullptr;
        long long v = strtoll(valstr, &end, 0);
        if (!end || *end) { up_log_first(sig, "fator int inválido '%s'", valstr); return false; }
        slot->ivalue = (int64_t)v;
    }
    uint32_t *thunk = up_thunk_page + (size_t)up_thunk_used * UP_MUL_THUNK_WORDS;
    int n = up_emit_mul_thunk(thunk, thunk, slot, is_float);
    if (n != UP_MUL_THUNK_WORDS) { up_log_first(sig, "erro interno no thunk"); return false; }
    __builtin___clear_cache((char *)thunk, (char *)(thunk + n));
    if (DobbyHook(code, (void *)thunk, &slot->orig) != 0) {
        up_log_first(sig, "DobbyHook falhou @%p", code);
        return false;
    }
    up_log("%s: mul aplicado @%p (thunk %d)", sig, code, up_thunk_used);
    up_thunk_used++;
    return true;
}

static bool up_apply_static(const Il2Cpp &il, void *klass, const up_rule_t *r, const char *valstr, const char *sig) {
    void *f = il.class_get_field_from_name(klass, r->member);
    if (!f) { up_log_first(sig, "campo %s::%s não achado", r->cls, r->member); return false; }
    // field_static_set_value num campo de instância é UB: escreveria no
    // endereço do FieldInfo tratado como dados. Recusa com log.
    if (!il.field_get_flags || (il.field_get_flags(f) & 0x10) == 0) {
        up_log_first(sig, "campo %s::%s não é static — regra recusada", r->cls, r->member);
        return false;
    }
    uint8_t bytes[8] = {};
    size_t size = 0;
    if (r->type == UP_BOOL) {
        int b = 0;
        if (!up_parse_bool(valstr, &b)) { up_log("%s: bool inválido '%s'", sig, valstr); return false; }
        bytes[0] = (uint8_t)b;
        size = 1;
    } else if (r->type == UP_INT) {
        uint32_t v = 0;
        if (!up_parse_int32(valstr, &v)) { up_log("%s: int inválido '%s'", sig, valstr); return false; }
        memcpy(bytes, &v, 4);
        size = 4;
    } else {
        char *end = nullptr;
        float fl = strtof(valstr, &end);
        if (!end || *end || fl != fl) { up_log_first(sig, "float inválido '%s'", valstr); return false; }
        memcpy(bytes, &fl, 4);
        size = 4;
    }
    il.field_static_set_value(f, bytes);
    if (up_statics_n < UP_STATIC_MAX) {
        up_static_t *st = &up_statics[up_statics_n++];
        st->klass = klass;  // revalidação (18): FieldInfo pode ficar obsoleto
        snprintf(st->cls, sizeof(st->cls), "%s", r->cls);
        snprintf(st->member, sizeof(st->member), "%s", r->member);
        memcpy(st->bytes, bytes, size);
        st->size = size;
        snprintf(st->sig, sizeof(st->sig), "%s", sig);
        up_log("%s: static fixado (%zu bytes, reaplica a cada 2s)", sig, size);
    } else {
        up_log_first(sig, "static aplicado 1x (teto de %d, sem reaplicação)", UP_STATIC_MAX);
    }
    return true;
}

// Hooka UM método pra fixar o campo: thunk escreve bytes em [x0+off] e
// segue pro original (tail call). Método estático não tem this — recusado.
static bool up_hook_field_method(void *m, const char *mname,
                                 size_t off, const uint8_t *bytes, size_t size, const char *sig) {
    void *code = *(void **)m;
    if (!code) { up_log_first(sig, "método %s sem código", mname); return false; }
    // Dobby troca o prólogo por salto (~16 bytes): método menor não tem onde.
    if (!up_method_fits((const uint32_t *)code, 4)) {
        up_log_first(sig, "metodo curto demais pra hook Dobby @%p, pulando", code);
        return false;
    }
    if (up_field_used >= UP_FIELD_MAX) {
        up_log_first(sig, "teto de %d thunks field, ignorada", UP_FIELD_MAX);
        return false;
    }
    if (!up_field_page) { up_log_first(sig, "sem página de thunk (mmap falhou)"); return false; }
    uint32_t bits = 0;
    memcpy(&bits, bytes, size);
    up_fslot_t *slot = &up_fslots[up_field_used];
    memset(slot, 0, sizeof(*slot));
    uint32_t *thunk = up_field_page + (size_t)up_field_used * UP_FIELD_THUNK_WORDS_MAX;
    int n = up_emit_field_thunk(thunk, thunk, slot, (int)size, bits, (uint32_t)off);
    if (n <= 0) {
        up_log_first(sig, "offset %zu fora do alcance do str (método %s), pulando", off, mname);
        return false;
    }
    __builtin___clear_cache((char *)thunk, (char *)(thunk + n));
    if (DobbyHook(code, (void *)thunk, &slot->orig) != 0) {
        up_log_first(sig, "DobbyHook falhou @%p", code);
        return false;
    }
    up_log("%s: field aplicado @%p (thunk %d, antes de %s)", sig, code, up_field_used, mname);
    up_field_used++;
    return true;
}

// field <Classe> <campo> <tipo> <valor> [<Método> <nargs>]: a cada chamada
// do método (instância), escreve this.campo = valor ANTES do original.
// Sem método: até 8 métodos de instância da classe que passam na guarda.
static bool up_apply_field(const Il2Cpp &il, void *klass, const up_rule_t *r, const char *valstr,
                           const char *sig) {
    void *f = il.class_get_field_from_name(klass, r->member);
    if (!f) { up_log_first(sig, "campo %s::%s não achado", r->cls, r->member); return false; }
    // field_static_set_value num campo de instância é UB (e vice-versa):
    // static aqui é recusado — pra static existe o verbo `static`.
    if (!il.field_get_flags || (il.field_get_flags(f) & 0x10) != 0) {
        up_log_first(sig, "campo %s::%s não é de instância — regra recusada", r->cls, r->member);
        return false;
    }
    uint8_t bytes[4] = {};
    size_t size = 0;
    if (r->type == UP_BOOL) {
        int b = 0;
        if (!up_parse_bool(valstr, &b)) { up_log_first(sig, "bool inválido '%s'", valstr); return false; }
        bytes[0] = (uint8_t)b;
        size = 1;
    } else if (r->type == UP_INT) {
        uint32_t v = 0;
        if (!up_parse_int32(valstr, &v)) { up_log_first(sig, "int inválido '%s'", valstr); return false; }
        memcpy(bytes, &v, 4);
        size = 4;
    } else {
        char *end = nullptr;
        float fl = strtof(valstr, &end);
        if (!end || *end || fl != fl) { up_log_first(sig, "float inválido '%s'", valstr); return false; }
        memcpy(bytes, &fl, 4);
        size = 4;
    }
    size_t off = il.field_get_offset(f);
    // Método explícito: valida e hooka um só.
    if (r->fmethod[0]) {
        if (!il.method_get_flags) {
            up_log_first(sig, "sem method_get_flags neste il2cpp — field explícito recusado");
            return false;
        }
        void *m = il.class_get_method_from_name(klass, r->fmethod, r->nargs);
        if (!m) {
            up_log_first(sig, "método %s::%s(%d) não achado", r->cls, r->fmethod, r->nargs);
            return false;
        }
        const char *mn = il.method_get_name ? il.method_get_name(m) : r->fmethod;
        if (mn && (strcmp(mn, ".ctor") == 0 || strcmp(mn, ".cctor") == 0)) {
            up_log_first(sig, "construtor não serve pra field (roda antes dos inicializadores)");
            return false;
        }
        uint32_t iflags = 0;
        if ((il.method_get_flags(m, &iflags) & 0x10) != 0) {
            up_log_first(sig, "método %s é static (sem this) — regra recusada", r->fmethod);
            return false;
        }
        return up_hook_field_method(m, mn ? mn : r->fmethod, off, bytes, size, sig);
    }
    // Auto: até 8 métodos de instância (sem ctor) que passam na guarda.
    if (!il.class_get_methods || !il.method_get_name || !il.method_get_flags) {
        up_log_first(sig, "sem API de enumeração neste il2cpp — field auto recusado");
        return false;
    }
    void *iter = nullptr, *m = nullptr;
    int hooked = 0, seen = 0;
    while (hooked < 8 && (m = il.class_get_methods(klass, &iter)) != nullptr) {
        const char *mn = il.method_get_name(m);
        if (!mn) continue;
        if (strcmp(mn, ".ctor") == 0 || strcmp(mn, ".cctor") == 0) continue;
        uint32_t iflags = 0;
        if ((il.method_get_flags(m, &iflags) & 0x10) != 0) continue;
        seen++;
        char msig[256];
        snprintf(msig, sizeof(msig), "%s@%s", sig, mn);
        if (up_hook_field_method(m, mn, off, bytes, size, msig)) hooked++;
    }
    if (!hooked) {
        up_log_first(sig, "nenhum método hookável pra field %s::%s", r->cls, r->member);
        return false;
    }
    up_log("%s: field aplicado em %d método(s) (%d visto(s))", sig, hooked, seen);
    return true;
}

// Uma passada: lê todo *.patch (menos *.off) + <id>.conf, aplica regra nova.
// Retorna quantas regras aplicou.
static int up_scan_apply(const Il2Cpp &il) {
    int applied = 0;
    struct dirent **ents = nullptr;
    int n = scandir(up_dir, &ents, nullptr, alphasort);
    if (n < 0) return 0;
    for (int i = 0; i < n; i++) {
        const char *name = ents[i]->d_name;
        bool is_patch = up_ends_with(name, ".patch") && !up_ends_with(name, ".off");
        if (!is_patch) { free(ents[i]); continue; }
        char id[128] = {};
        size_t baselen = strlen(name) - 6;  // tira ".patch"
        if (baselen >= sizeof(id)) baselen = sizeof(id) - 1;
        memcpy(id, name, baselen);
        char ppath[448], cpath[448];
        snprintf(ppath, sizeof(ppath), "%s/%s", up_dir, name);
        snprintf(cpath, sizeof(cpath), "%s/%s.conf", up_dir, id);
        char *pbuf = up_read_file(ppath, nullptr);
        char *cbuf = up_read_file(cpath, nullptr);  // .conf opcional
        if (!pbuf) { free(ents[i]); free(cbuf); continue; }
        // Itera linhas sem strtok no buffer inteiro (parse destrói a linha).
        char *line = pbuf;
        int lineno = 0;
        while (line) {
            lineno++;
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            up_rule_t r;
            int pr = up_parse_line(line, &r);
            if (pr == 0) {
                char sig[224];
                up_sig(sig, sizeof(sig), id, &r);
                int seen = up_find_applied(sig);
                if (seen < 0 || up_applied[seen].state == 1) {
                    char valstr[64];
                    if (!up_resolve_value(r.value, cbuf, valstr, sizeof(valstr))) {
                        if (seen < 0) up_log("%s:%d: $%s sem valor no %s.conf", id, lineno, r.value + 1, id);
                        else up_mark(sig, 1);  // já logou na 1ª passada
                    } else {
                        char nspace[128], cname[128];
                        void *klass = nullptr;
                        if (up_split_class(r.cls, nspace, sizeof(nspace), cname, sizeof(cname)))
                            klass = il.find_class(nspace, cname);
                        bool ok = false;
                        if (!klass) {
                            if (seen < 0) up_log("%s:%d: classe %s não encontrada", id, lineno, r.cls);
                            else up_mark(sig, 1);  // silêncio nas passadas seguintes
                        } else if (r.kind == UP_RETURN) {
                            ok = up_apply_return(il, klass, &r, valstr, sig);
                        } else if (r.kind == UP_MUL) {
                            ok = up_apply_mul(il, klass, &r, valstr, sig);
                        } else if (r.kind == UP_FIELD) {
                            ok = up_apply_field(il, klass, &r, valstr, sig);
                        } else {
                            ok = up_apply_static(il, klass, &r, valstr, sig);
                        }
                        if (ok) { up_mark(sig, 2); applied++; }
                        else if (seen < 0) up_log("%s:%d: regra falhou, tenta de novo em silêncio", id, lineno);
                    }
                }
            } else if (pr < 0) {
                up_log("%s:%d: linha inválida, ignorando", id, lineno);
            }
            // pr == 1 (vazia/comentário) só pula o bloco acima — line AVANÇA
            // sempre, senão loop infinito (revisão do kilo).
            line = nl ? nl + 1 : nullptr;
            if (line && !*line) line = nullptr;
        }
        free(pbuf);
        free(cbuf);
        free(ents[i]);
    }
    free(ents);
    return applied;
}

// Reaplicação de statics (18): re-resolve CLASSE+CAMPO a cada passada
// (recompilar só quando o namespace difere deixa obsoleto por outros
// motivos — recarga de assembly, domínio novo). Ponteiro mudou = atualiza
// e loga uma vez; não achou mais = sai da lista.
static void up_reapply_statics(Il2Cpp &il) {
    int w = 0;
    for (int i = 0; i < up_statics_n; i++) {
        up_static_t *st = &up_statics[i];
        char nspace[128], cname[128];
        void *k = nullptr, *f = nullptr;
        if (up_split_class(st->cls, nspace, sizeof(nspace), cname, sizeof(cname))) {
            k = il.find_class(nspace, cname);
            f = k ? il.class_get_field_from_name(k, st->member) : nullptr;
        }
        if (f && il.field_get_flags && (il.field_get_flags(f) & 0x10) == 0) f = nullptr;
        if (!f) {
            up_log("%s: static %s.%s não revalidou — reaplicação cancelada", st->sig, st->cls, st->member);
            continue;  // cai fora da lista compactada
        }
        if (f != st->field) {
            up_log("%s: static %s.%s mudou de endereço (%p -> %p), atualizando",
                   st->sig, st->cls, st->member, st->field, f);
        }
        st->klass = k;
        st->field = f;
        il.field_static_set_value(f, st->bytes);
        up_statics[w++] = *st;
    }
    up_statics_n = w;
}

static void *up_worker(void *) {
    // O constructor pode ter rodado ainda no zygote (sem env e com cmdline
    // inútil): RETRY REAL — re-tenta cmdline a cada 1s até sair de zygote*.
    int waits = 0;
    while (!up_pkg[0] && !up_read_pkg()) {
        if (++waits % 60 == 1) up_log("sem pacote resolvido ainda, aguardando");
        sleep(1);
    }
    up_log("carregado, esperando libil2cpp.so");
    Il2Cpp il;
    if (!il2cpp_boot(il)) { up_log("il2cpp não subiu em ~240s (120s lib + 120s domínio) — desistindo"); return nullptr; }
    // Página RX pros thunks mul (código separado dos dados).
    up_thunk_page = (uint32_t *)mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (up_thunk_page == MAP_FAILED) up_thunk_page = nullptr;
    // Página RX pros thunks field (mesmo esquema, pool separado).
    up_field_page = (uint32_t *)mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (up_field_page == MAP_FAILED) up_field_page = nullptr;
    up_log("il2cpp ok, dir %s", up_dir);
    for (;;) {
        int n = up_scan_apply(il);
        if (n > 0) up_log("%d regra(s) nova(s) aplicada(s)", n);
        up_reapply_statics(il);
        sleep(2);  // jogo pode resetar static; regra nova pode ter chegado
    }
    return nullptr;
}

__attribute__((constructor)) static void u_patch_init() {
    // Best-effort no constructor: com env do loader resolve aqui (antes do
    // pthread_create); sem env, o worker re-tenta cmdline até sair de zygote*.
    up_read_pkg();
    pthread_t t;
    if (pthread_create(&t, nullptr, up_worker, nullptr) == 0) pthread_detach(t);
}
