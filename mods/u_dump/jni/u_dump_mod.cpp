// u_dump — scanner universal Unity IL2CPP (F3). Gera o dump.tsv do contrato
// C5 via API runtime do il2cpp (imune a metadata v39/criptografada: não lê
// o global-metadata.dat, pergunta o runtime vivo):
//   # pkg=<pkg> il2cpp_size=<bytes> unity=<versão se achar>
//   C  <assembly>  <Namespace.Classe>
//   M  <Namespace.Classe>  <método>  <nargs>  <tipo_retorno>  <static 0|1>
//   F  <Namespace.Classe>  <campo>  <tipo>  <static 0|1>  <offset>
// Só gera se o arquivo ainda não existe (refazer = Manager apaga e reinicia
// o jogo). Escreve em "<final>.tmp" e rename() no fim (fflush+fsync antes):
// o Manager nunca lê dump pela metade. Thread própria com yield entre
// assemblies: não trava o jogo (boot do il2cpp é lá dentro — domain_get
// antes do init crasha, sa2ammo).
//
// pkg e log (C1, F2): mod_common.h. O loader Zygisk posta BEPINEX_PKG antes
// do dlopen (jni/main.cpp:1800), então o pacote resolve no constructor; o
// fallback por cmdline do mod_common (que rejeita "zygote64") só entra se a
// env faltar, e aí o wait_for_pkg() abaixo espera. Linha de log no mesmo
// formato de antes: "HH:MM:SS [u_dump] msg" em files/bepinex/log.txt.
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include "../../common/il2cpp_min.h"
#include "../../common/dump_core.h"
#include "../../common/mod_common.h"

#define TAG "u_dump"
#define LOG(...) mod_log(TAG, __VA_ARGS__)

static Il2Cpp il;

// --- C1: pacote e log (mod_common, F2) ---
//
// O C1 inteiro deste mod era duplicado (log_open_append + c1_log +
// read_cmdline + resolve_pkg). Agora é o mod_common: mod_log escreve no
// logcat E em files/bepinex/log.txt, e mod_pkg resolve o pacote. O FORMATO
// DA LINHA NÃO MUDA: era "HH:MM:SS [u_dump] msg" e continua (o mod_common
// monta a mesma linha, e test/mod_common_test.cpp fixa o formato).
//
// O que o mod perde com a troca: nada de comportamento. O que ganha: o
// mod_common também cai no logcat quando não tem pkg (degradação silenciosa
// em vez de abrir arquivo com path quebrado), e o teto de rotação passa a
// ser o do contrato (256KB) em vez de um número copiado aqui.

// Espera o pacote do contrato C1. Teto de 120s, nunca infinito.
//
// Evidência de que no caminho normal isto volta na primeira tentativa: o
// loader posta BEPINEX_PKG em jni/main.cpp:1800, dentro de
// load_dynamic_mods(), logo ANTES do loop de dlopen (o comentário de lá
// diz isso: "o constructor do mod lê no dlopen"). Então, no caminho
// genérico, a env já está posta quando este constructor roda.
//
// O retry fica para o resto: mod_pkg() cai no cmdline só quando a env
// falta, e nesse caso pode ser cedo demais (cmdline ainda "zygote64",
// rejeitado por mod_pkg_from_cmdline). Antes isto era um for(;;) sem teto
// — e um teto curto (10s, o que a revisão dedevice reclamou) matava o
// dump em device lento. 120s é o mesmo orçamento do il2cpp_boot logo
// abaixo. Estourou o teto? O dump é abortado: um dump.tsv com
// "# pkg=" vazio não serve para nada.
static const char *wait_for_pkg(int timeout_s) {
    for (int i = 0; i <= timeout_s * 5; i++) {
        const char *p = mod_pkg();
        if (p) return p;
        usleep(200 * 1000);
    }
    return nullptr;
}

// Tamanho do mapeamento da libil2cpp (header do C5): soma os PT_LOAD.
static int il2cpp_size_cb(struct dl_phdr_info *info, size_t, void *out) {
    if (info->dlpi_name && strstr(info->dlpi_name, "/libil2cpp.so")) {
        long long total = 0;
        for (int i = 0; i < info->dlpi_phnum; i++)
            if (info->dlpi_phdr[i].p_type == PT_LOAD)
                total += (long long)info->dlpi_phdr[i].p_memsz;
        *(long long *)out = total;
        return 1;
    }
    return 0;
}

static long long il2cpp_mapped_size() {
    long long sz = 0;
    dl_iterate_phdr(il2cpp_size_cb, &sz);
    return sz;
}

// --- il2cpp -> C5 ---

// "Namespace.Externa/Interna" (C5 2026-09-26+): namespace e 1º nome vêm da
// raiz; caminho de nesting com '/', subindo class_get_declaring_type até a
// raiz. Sem o símbolo (il2cpp velho): nome curto, como antes.
static const char *full_class_name(void *klass, char *out, size_t cap) {
    if (!il.class_get_declaring_type) {
        const char *ns = il.class_get_namespace(klass);
        const char *nm = il.class_get_name(klass);
        if (!nm) { snprintf(out, cap, "?"); return out; }
        if (!ns || !ns[0]) { snprintf(out, cap, "%s", nm); return out; }
        snprintf(out, cap, "%s.%s", ns, nm);
        return out;
    }
    const char *parts[256];  // C5: namespace vem da RAIZ de verdade.
    // Sobe a cadeia inteira (cadeia real é curta e finita); teto 256 só
    // contra metadata corrompida em loop — aí a linha vira "?" em vez de
    // sair com namespace do meio (bug da revisão).
    int nparts = 0;
    void *k = klass, *root = klass;
    while (k && nparts < 256) {
        parts[nparts++] = il.class_get_name(k);
        root = k;
        k = il.class_get_declaring_type(k);
    }
    if (nparts == 0 || nparts == 256) { snprintf(out, cap, "?"); return out; }
    for (int i = 0; i < nparts / 2; i++) {  // coletei fundo→raiz; C5 é raiz→fundo
        const char *tn = parts[i];
        parts[i] = parts[nparts - 1 - i];
        parts[nparts - 1 - i] = tn;
    }
    dump_join_class_name(out, cap, il.class_get_namespace(root), parts, nparts);
    return out;
}

// unity= do header C5 ("versão se achar"): UnityEngine.Application.
// get_unityVersion via runtime_invoke. Seguro fora da main thread: thread
// anexada (il2cpp_boot) + getter estático folha, o mesmo mecanismo das
// calls do sa2content (Apply/Deserialize na worker). Qualquer falha =
// string vazia (comportamento anterior). String il2cpp é UTF-16 em
// +0x14/len +0x10, mesmo layout do il2cpp_str_eq (il2cpp_min.h).
static bool unity_version_str(char *out, size_t cap) {
    out[0] = '\0';
    void *app = il.find_class("UnityEngine", "Application");
    if (!app) return false;
    bool ok = false;
    void *s = il.call_static(app, "get_unityVersion", nullptr, 0, &ok);
    if (!ok || !s || cap < 8) return false;
    int32_t len = *(const int32_t *)((const uint8_t *)s + 0x10);
    const uint16_t *c = (const uint16_t *)((const uint8_t *)s + 0x14);
    if (len <= 0 || len >= (int32_t)cap) return false;
    for (int32_t i = 0; i < len; i++) {
        if (c[i] > 0x7E) return false;  // versão é ASCII ("2022.3.21f1")
        out[i] = (char)c[i];
    }
    out[len] = '\0';
    bool dot = false, dig = false;  // parece versão? exige dígito + ponto
    for (int32_t i = 0; i < len; i++) {
        if (out[i] == '.') dot = true;
        if (out[i] >= '0' && out[i] <= '9') dig = true;
    }
    if (!dot || !dig) { out[0] = '\0'; return false; }
    return true;
}

// Nome legível do tipo (type_get_name aloca pelo runtime: copia e solta).
static const char *type_name_of(const void *type, char *buf, size_t cap) {
    if (!type) { snprintf(buf, cap, "?"); return buf; }
    char *n = il.type_get_name(type);
    if (!n) { snprintf(buf, cap, "?"); return buf; }
    snprintf(buf, cap, "%s", n);
    il.free(n);
    return buf;
}

// Uma classe -> 1 linha C + M/F por método/campo. Retorna nº de linhas.
static long long dump_class(FILE *out, void *image, void *klass, const char *cls_name) {
    const char *asm_name = il.image_get_name ? il.image_get_name(image) : nullptr;
    char line[768];
    long long n = 0;
    int w = dump_write_class(line, sizeof(line), asm_name, cls_name);
    fwrite(line, 1, (size_t)w, out);
    n++;

    // Métodos: iterador com cursor (void** estilo Java Enumeration). O cursor
    // (iter) é estado do runtime: NUNCA sobrescrever com o retorno (achado em
    // device: SIGSEGV em il2cpp_type_get_name — o próximo iter_* recebia o
    // MethodInfo* anterior como "cursor").
    if (il.class_get_methods && il.method_get_name) {
        void *iter = nullptr;
        void *m;
        while ((m = il.class_get_methods(klass, &iter)) != nullptr) {
            uint32_t iflags = 0;
            uint32_t flags = il.method_get_flags ? il.method_get_flags(m, &iflags) : 0;
            const char *ret = type_name_of(il.method_get_return_type(m), line, sizeof(line));
            char mline[896];
            w = dump_write_method(mline, sizeof(mline), cls_name,
                                  il.method_get_name(m), il.method_get_param_count(m),
                                  ret, (flags & dump_attr_static_mask()) != 0);
            fwrite(mline, 1, (size_t)w, out);
            n++;
        }
    }
    // Campos: offset do field_get_offset (0 é offset real de estático/literal).
    if (il.class_get_fields && il.field_get_name) {
        void *iter = nullptr;
        void *f;
        while ((f = il.class_get_fields(klass, &iter)) != nullptr) {
            const char *nm = il.field_get_name(f);
            if (!nm) continue;
            int fflags = il.field_get_flags ? il.field_get_flags(f) : 0;
            char tbuf[512];
            const char *t = type_name_of(il.field_get_type(f), tbuf, sizeof(tbuf));
            char fline[1024];
            w = dump_write_field(fline, sizeof(fline), cls_name, nm, t,
                                 (fflags & (int)dump_attr_static_mask()) != 0,
                                 (int)il.field_get_offset(f));
            fwrite(fline, 1, (size_t)w, out);
            n++;
        }
    }
    return n;
}

static void *worker(void *) {
    char msg[256];
    const char *pkg = wait_for_pkg(120);
    if (!pkg) {
        LOG("pacote (C1) não resolveu em 120s — dump abortado: o cabeçalho C5 "
            "exige # pkg=<pkg> e um dump sem pacote não serve para nada");
        return nullptr;
    }

    char dir[352], path[384], tmp[448];
    snprintf(dir, sizeof(dir), "/data/data/%s/files/bepinex", pkg);
    snprintf(path, sizeof(path), "%s/dump.tsv", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    struct stat st;
    if (stat(path, &st) == 0) {
        snprintf(msg, sizeof(msg), "dump.tsv já existe (%lld bytes) — nada a fazer",
                 (long long)st.st_size);
        LOG("%s", msg);
        return nullptr;
    }

    if (!il2cpp_boot(il)) {
        LOG("il2cpp não subiu em ~240s (120s lib + 120s domínio) — desistindo");
        return nullptr;
    }
    // Enumeração obrigatória (opcionais: flags/image_get_name, com fallback).
    if (!il.image_get_class_count || !il.image_get_class || !il.class_get_name ||
        !il.class_get_namespace || !il.type_get_name || !il.method_get_param_count ||
        !il.method_get_return_type || !il.class_get_methods || !il.class_get_fields) {
        LOG("API de enumeração ausente nesta libil2cpp — desistindo");
        return nullptr;
    }

    FILE *out = fopen(tmp, "w");
    if (!out) {
        snprintf(msg, sizeof(msg), "não abriu %s (SELinux/perm?) — desistindo", tmp);
        LOG("%s", msg);
        return nullptr;
    }

    size_t nasms = 0;
    void **asms = il.domain_get_assemblies(il.domain, &nasms);
    if (!asms || nasms == 0) {
        LOG("nenhum assembly no domínio — desistindo");
        fclose(out);
        unlink(tmp);
        return nullptr;
    }

    {
        char hdr[512], unity[64];
        unity_version_str(unity, sizeof(unity));  // vazio se não achar (C5: opcional)
        int w = dump_write_header(hdr, sizeof(hdr), pkg, il2cpp_mapped_size(), unity);
        fwrite(hdr, 1, (size_t)w, out);
    }

    // Thread própria + yield entre assemblies: o jogo não trava (F3).
    long long total = 0;
    for (size_t i = 0; i < nasms; i++) {
        void *image = il.assembly_get_image(asms[i]);
        if (!image) continue;
        size_t kcount = il.image_get_class_count(image);
        for (size_t k = 0; k < kcount; k++) {
            void *klass = il.image_get_class(image, k);
            if (!klass) continue;
            char cls[512];
            total += dump_class(out, image, klass, full_class_name(klass, cls, sizeof(cls)));
        }
        if ((i & 3) == 3) sched_yield();  // 1 a cada 4 assemblies
    }
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    if (rename(tmp, path) != 0) {
        snprintf(msg, sizeof(msg), "rename %s -> %s falhou — dump descartado", tmp, path);
        LOG("%s", msg);
        unlink(tmp);
        return nullptr;
    }
    snprintf(msg, sizeof(msg), "dump.tsv pronto: %lld linhas, %zu assemblies", total, nasms);
    LOG("%s", msg);
    return nullptr;
}

__attribute__((constructor)) static void u_dump_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
