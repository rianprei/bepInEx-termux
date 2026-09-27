// u_patch — motor declarativo (FASE F4): lê todo *.patch + <id>.conf de
// /data/local/tmp/mods/<pkg>/ e aplica as regras C4 (return/mul/static).
// Regra que não resolve vira log e o jogo segue. Nada de offset fixo: tudo
// sai da API il2cpp exportada. Log mínimo próprio (F2 dá mod_common.h e a
// gente troca — até lá, logcat + log.txt C1 aqui mesmo).
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
#include "../../common/mod_common.h"
#include "u_patch_parse.h"
#include "u_patch_arm64.h"
#include "u_patch_dedupe.h"
#include "u_patch_resolve.h"

#define UP_TAG "u_patch"
#define UP_MODS_DIR_FMT "/data/local/tmp/mods/%s"
#define UP_LOG_FMT "/data/data/%s/files/bepinex/log.txt"
#define UP_STATIC_MAX 32  // campos static fixados (reaplica a cada 2s)

static char up_pkg[128];
static char up_dir[320];

// Log C1: mod_common (SDK F2). up_log próprio saiu: ele rotacionava
// log.txt->log.txt.1 a cada linha acima de 256KB enquanto o loader e o
// mod_common ZERAM o mesmo arquivo, e o resultado era o "Ver log" do
// Manager perdendo o histórico do crash que o usuário estava depurando
// (achado #15 do review). Agora há UM dono da rotação: o do contrato C1.
//
// up_log continua existindo como alias porque os logs do u_patch ficam
// agrupados por assinatura e o formato do mod_common (HH:MM:SS [tag] msg)
// é o mesmo do loader.
static void up_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    mod_log(UP_TAG, "%s", msg);
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
#define UP_FILE_MAX 65535

// Lê o arquivo inteiro (até UP_FILE_MAX). Retorna malloc'd, *len = tamanho.
// Se *truncated vier não-nulo, ganha 1 quando o arquivo era MAIOR que o teto:
// sem esse aviso um .patch de 200KB entrava pela metade e o usuário via metade
// das regras sem efeito e nenhum log (achado #5 do review).
static char *up_read_file(const char *path, size_t *len, bool *truncated) {
    if (truncated) *truncated = false;
    FILE *f = fopen(path, "r");
    if (!f) return nullptr;
    char *buf = (char *)malloc(UP_FILE_MAX + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t n = fread(buf, 1, UP_FILE_MAX, f);
    int eof = feof(f);
    fclose(f);
    buf[n] = '\0';
    if (truncated) *truncated = (!eof && n == UP_FILE_MAX);
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
    if (mprotect((void *)p0, (size_t)(p1 - p0 + ps), PROT_READ | PROT_EXEC) != 0) {
        // Antes devolvia true e a página ficava RWX até o processo morrer
        // (achado #9 do review). Agora é falha: o log diz e a próxima
        // passada de 2s tenta de novo.
        up_log("mprotect RX de volta falhou em %p (%s) — página ficou RWX, o patch foi aplicado "
               "mas a próxima passada tenta reverter", (void *)p0, strerror(errno));
        return false;
    }
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

// Chave = hash do texto completo da regra (u_patch_dedupe.h): sem truncamento
// (achado #7) e sem inundação de log quando a tabela enche (achado #6).
static up_applied_t up_applied[UP_APPLIED_MAX];
static int up_applied_n;
static bool up_table_full_logged;

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
    return up_dedupe_find(up_applied, up_applied_n, up_sig_hash(sig));
}

static void up_mark(const char *sig, uint8_t state) {
    uint64_t hash = up_sig_hash(sig);
    if (up_dedupe_mark(up_applied, &up_applied_n, UP_APPLIED_MAX, hash, state)) return;
    if (!up_table_full_logged) {
        up_table_full_logged = true;
        up_log("tabela de %d regras cheia: regras novas NÃO são lembradas (não voltam a ser "
               "aplicadas nem logadas a cada 2s). Reduza o .patch ou divida em mais arquivos.",
               UP_APPLIED_MAX);
    }
}

// Log de falha só na 1ª vez por regra (parcial 11): a mesma regra é
// reavaliada a cada 2s; sem isso ela enchia o log a cada passada.
static void up_log_first(const char *sig, const char *fmt, ...) {
    if (!up_dedupe_should_log(up_applied, up_applied_n, up_sig_hash(sig))) return;  // já logou
    va_list ap;
    char msg[448];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    up_log("%s", msg);
    up_mark(sig, UP_ST_SEEN);  // "falhou/logou" — sucesso sobrescreve com UP_ST_OK
}

// Texto legível da regra (vai no log) montado num buffer folgado: o antigo
// char[224] truncava classe+membro+valor+método (~456 bytes) e colidia.
static void up_sig(char *out, size_t n, const char *id, const up_rule_t *r) {
    char full[768];
    int w;
    if (r->kind == UP_FIELD)
        w = snprintf(full, sizeof(full), "%s|%d|%s|%s|%d|%d|%s|%s", id, (int)r->kind, r->cls,
                     r->member, r->nargs, (int)r->type, r->value, r->fmethod);
    else
        w = snprintf(full, sizeof(full), "%s|%d|%s|%s|%d|%d|%s", id, (int)r->kind, r->cls,
                     r->member, r->nargs, (int)r->type, r->value);
    if (w < 0) { full[0] = '\0'; }
    else if ((size_t)w >= sizeof(full)) full[sizeof(full) - 1] = '\0';
    snprintf(out, n, "%s", full);
}

static bool up_apply_return(const Il2Cpp &il, void *klass, const up_rule_t *r,
                            const char *valstr, const char *sig) {
    void *m = il.class_get_method_from_name(klass, r->member, r->nargs);
    if (!m) { up_log_first(sig, "método %s::%s(%d) não achado", r->cls, r->member, r->nargs); return false; }
    void *code = *(void **)m;  // methodPointer = 1º campo (il2cpp >= 2019.3, il2cpp-class.h; antes tinha um struct MethodInfo prefixado).
    if (!code) { up_log_first(sig, "método sem código (genérico/abstrato?)"); return false; }
    if (*(uint32_t *)code == UP_RET) { up_log_first(sig, "método já é só ret, pulando"); return false; }
    // #11: o C4 float escreve s0 (32 bits). Num método que devolve double o
    // valor volta em d0 e o fmov/fmul zera a metade alta → o jogo recebe um
    // double adulterado sem nenhum crash para reclamar. O il2cpp_min já
    // expõe method_get_return_type + type_get_name.
    if (r->type == UP_FLOAT && il.method_get_return_type && il.class_from_type && il.type_get_name) {
        void *rt = (void *)il.method_get_return_type(m);
        // type_get_name recebe o TYPE (rt), não a classe — o mesmo type
        // confusion que derrubou o jogo no caminho de `field`
        // (ver up_field_type_name). rk é só a prova de que o tipo resolve.
        void *rk = rt ? il.class_from_type(rt) : nullptr;
        if (rk) {
            char *rn = il.type_get_name(rt);
            if (rn) {
                if (strcmp(rn, "System.Double") == 0) {
                    up_log_first(sig, "método %s::%s devolve System.Double (64 bits) e a regra "
                                      "escreve s0 (32 bits): metade alta do retorno viraria zero "
                                      "sem erro visível — regra recusada", r->cls, r->member);
                    if (il.free) il.free(rn); else free(rn);
                    return false;
                }
                if (il.free) il.free(rn); else free(rn);
            }
        }
    }
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

static bool up_apply_mul(const Il2Cpp &il, void *klass, const up_rule_t *r,
                         const char *valstr, const char *sig) {
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

// Recusa ANTES de hookar quando o tamanho do tipo real não bate com o da
// regra (achado #2): escrever 4 bytes num campo de 1 byte suja os vizinhos do
// objeto, e o static reescreve isso a cada 2s.
//
// `why` (se não-nulo) recebe a frase de recusa completa, com o motivo do elo
// que falhou na resolução do tipo — para o log.txt dizer se o campo não
// existe, se é genérico, ou se a API de tipo não está neste il2cpp.
static bool up_type_guard(const Il2Cpp &il, void *klass, void *field, size_t want,
                          bool is_instance, const char *sig, const char *cls,
                          const char *member) {
    char name[128];
    up_resolve_status st = up_resolve_field_type(&il, field, name, sizeof(name));
    char why[384];
    if (st != UP_RS_OK) {
        // Motivo do elo que falhou. UP_RS_NO_API não é erro: o il2cpp é velho
        // e não expõe a API de tipo — aí o comportamento antigo (escrever sem
        // checar tamanho) é mantido, e o log diz isso explicitamente.
        up_resolve_log_line(st, cls, member, why, sizeof(why));
        up_log_first(sig, "%s: %s", is_instance ? "field recusado" : "static recusado", why);
        return false;
    }
    if (up_value_type_check(il.class_is_valuetype && il.class_is_valuetype(klass),
                            name, want, why, sizeof(why)) != 0) {
        up_log_first(sig, "%s: %s", is_instance ? "field recusado" : "static recusado", why);
        return false;
    }
    return true;
}

static bool up_apply_static(const Il2Cpp &il, void *klass, const up_rule_t *r,
                            const char *valstr, const char *sig) {
    void *f = il.class_get_field_from_name(klass, r->member);
    if (!f) { up_log_first(sig, "campo %s::%s não achado", r->cls, r->member); return false; }
    // field_static_set_value num campo de instância é UB: escreveria no
    // endereço do FieldInfo tratado como dados. Recusa com log.
    if (!il.field_get_flags || (il.field_get_flags(f) & 0x10) == 0) {
        up_log_first(sig, "campo %s::%s não é static — regra recusada", r->cls, r->member);
        return false;
    }
    // Tamanho do tipo real confere com o da regra ANTES de escrever (#2).
    {   // static: o valor da regra decide o size logo abaixo; para descobrir o
        // size esperado, reparseia o tipo da regra (bool=1, int/float=4).
        size_t want = (r->type == UP_BOOL) ? 1 : 4;
        if (!up_type_guard(il, klass, f, want, false, sig, r->cls, r->member)) return false;
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
    up_log("%s: field aplicado @%p (thunk %d, antes de %s; valor embutido no thunk — mudar o "
           ".conf pede reinstalar)", sig, code, up_field_used, mname);
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
    // Tamanho real do campo x tamanho da regra + classe de valor? (achados
    // #2 e #3) — ANTES de hookar qualquer método, porque depois do hook a
    // escrita acontece a cada chamada, com this do jogo.
    {   size_t want = (r->type == UP_BOOL) ? 1 : 4;
        if (!up_type_guard(il, klass, f, want, true, sig, r->cls, r->member)) return false; }
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
        // Chave do auto: hash(sig + "@" + método) — o texto só serve de log.
        char msig[768];
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

// Corpo de UMA linha do .patch (chamado por up_foreach_line, que garante o
// avanço — o loop infinito em linha vazia do review não tem mais onde
// nascer, e o Caso 70 do harness conta as linhas visitadas).
struct up_line_ctx {
    const Il2Cpp *il;
    const char *id;
    const char *cbuf;
    int applied;
};

static int up_line_apply(char *line, int lineno, void *vctx) {
    up_line_ctx *c = (up_line_ctx *)vctx;
    const Il2Cpp &il = *c->il;
    const char *id = c->id;
    up_rule_t r;
    int pr = up_parse_line(line, &r);
    if (pr == 0) {
        char sig[768];
        up_sig(sig, sizeof(sig), id, &r);
        int seen = up_find_applied(sig);
        if (seen < 0 || up_applied[seen].state == UP_ST_SEEN) {
            char valstr[64];
            if (!up_resolve_value(r.value, c->cbuf, valstr, sizeof(valstr))) {
                if (seen < 0) up_log("%s:%d: $%s sem valor no %s.conf", id, lineno, r.value + 1, id);
                else up_mark(sig, UP_ST_SEEN);
            } else {
                char nspace[128], cname[128];
                void *klass = nullptr;
                if (up_split_class(r.cls, nspace, sizeof(nspace), cname, sizeof(cname)))
                    klass = il.find_class(nspace, cname);
                if (!klass) {
                    if (seen < 0) up_log("%s:%d: classe %s não encontrada", id, lineno, r.cls);
                    else up_mark(sig, UP_ST_SEEN);
                    return 0;
                }
                // Rastro ANTES de qualquer passo arriscado.
                //
                // ACHADO REAL (device): na hora do crash o log.txt estava SEM
                // nenhuma linha [u_patch] — a primeira linha de log saía
                // DEPOIS da resolução (up_log_first só dispara no fim), então
                // o crash apagava o caminho inteiro e sobrava um tombstone sem
                // dizer qual regra estava em jogo. Agora o log.txt diz.
                //
                // Gate em `seen < 0` (regra nunca processada) e NÃO no
                // up_log_first: consumir o slot de dedupe aqui faria a linha
                // de falha ("campo X não encontrado") ser suprimida — e é
                // justamente ela que o usuário precisa ver. Uma vez por
                // regra, sem inundar o log a cada 2s.
                if (seen < 0) up_log("u_patch: resolvendo %s", sig);
                bool ok = false;
                if (r.kind == UP_RETURN) {
                    ok = up_apply_return(il, klass, &r, valstr, sig);
                } else if (r.kind == UP_MUL) {
                    ok = up_apply_mul(il, klass, &r, valstr, sig);
                } else if (r.kind == UP_FIELD) {
                    ok = up_apply_field(il, klass, &r, valstr, sig);
                } else {
                    ok = up_apply_static(il, klass, &r, valstr, sig);
                }
                if (ok) { up_mark(sig, UP_ST_OK); c->applied++; }
                else if (seen < 0) up_log("%s:%d: regra falhou, tenta de novo em silêncio", id, lineno);
            }
        }
    } else if (pr < 0) {
        up_log("%s:%d: linha inválida, ignorando", id, lineno);
    }
    return 0;  // pr == 1 (vazia/comentário) só não entra no bloco acima
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
        bool trunc = false;
        char *pbuf = up_read_file(ppath, nullptr, &trunc);
        char *cbuf = up_read_file(cpath, nullptr, nullptr);  // .conf opcional
        if (!pbuf) { free(ents[i]); free(cbuf); continue; }
        if (trunc) {
            // #5: sem este aviso, um .patch de 200KB entrava pela metade e o
            // usuário via metade das regras sem efeito nenhum.
            up_log("%s: arquivo maior que %d bytes — truncado em %d, as regras depois "
                   "desse byte NAO foram lidas", name, UP_FILE_MAX, UP_FILE_MAX);
        }
        // Itera linhas com up_foreach_line: o avanço mora na função pura e o
        // harness conta as linhas (Caso 70) — o loop infinito em linha vazia
        // não tem mais onde nascer.
        up_line_ctx ctx = {&il, id, cbuf, 0};
        up_foreach_line(pbuf, up_line_apply, &ctx, 0);
        applied += ctx.applied;
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
// #10: revalidar (find_class varre as ~162k classes do SA2) a cada 2s por
// static era custo real de CPU/bateria. O VALOR continua sendo reescrito a
// cada passada (isso é barato e é o que o C4 promete: "reaplica a cada 2s");
// só a re-resolução do FieldInfo virou uma a cada 10 passadas (~20s).
#define UP_STATIC_REVALIDATE_EVERY 10
static int up_static_pass;

static void up_reapply_statics(Il2Cpp &il) {
    static int revalidate_at = UP_STATIC_REVALIDATE_EVERY;
    bool revalidate = (++up_static_pass % revalidate_at) == 0;
    int w = 0;
    for (int i = 0; i < up_statics_n; i++) {
        up_static_t *st = &up_statics[i];
        void *f = st->field;
        if (revalidate) {
            char nspace[128], cname[128];
            void *k = nullptr;
            if (up_split_class(st->cls, nspace, sizeof(nspace), cname, sizeof(cname))) {
                k = il.find_class(nspace, cname);
                f = k ? il.class_get_field_from_name(k, st->member) : nullptr;
            }
            if (f && il.field_get_flags && (il.field_get_flags(f) & 0x10) == 0) f = nullptr;
            st->klass = k;
        }
        if (!f) {
            up_log("%s: static %s.%s não revalidou — reaplicação cancelada", st->sig, st->cls, st->member);
            continue;  // cai fora da lista compactada
        }
        if (f != st->field) {
            up_log("%s: static %s.%s mudou de endereço (%p -> %p), atualizando",
                   st->sig, st->cls, st->member, st->field, f);
        }
        st->field = f;
        il.field_static_set_value(f, st->bytes);
        up_statics[w++] = *st;
    }
    up_statics_n = w;
}

static void *up_worker(void *) {
    // O constructor pode ter rodado ainda no zygote (sem env e com cmdline
    // inútil): RETRY REAL — re-tenta cmdline a cada 1s até sair de zygote*.
    // Teto de 120s, mesmo orçamento do il2cpp_boot logo abaixo. O retry sem
    // teto (achado #8 do review) deixava a thread viva para sempre num
    // processo que nunca especializa, com 1 linha de log por minuto.
    int waits = 0;
    while (!up_pkg[0] && !up_read_pkg() && waits < 120) {
        if (waits == 0) up_log("sem BEPINEX_PKG nem cmdline de app ainda, aguardando (teto 120s)");
        sleep(1);
        waits++;
    }
    if (!up_pkg[0]) {
        up_log("pacote (C1) não resolveu em 120s — u_patch não aplica nada; "
               "o jogo segue sem os mods deste .patch");
        return nullptr;
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
