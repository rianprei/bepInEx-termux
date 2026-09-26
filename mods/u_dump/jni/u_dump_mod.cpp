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
// pkg (C1 2026-09-26+): getenv("BEPINEX_PKG") primeiro (o loader Zygisk seta
// antes do dlopen); /proc/self/cmdline no constructor ainda é "zygote64"
// (achado em device), então o fallback espera o cmdline virar o do app.
// Log mínimo próprio (mod_common.h chega na F2): logcat tag "u_dump" +
// append em files/bepinex/log.txt (formato C1: HH:MM:SS [u_dump] msg).
#include <android/log.h>
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

#define TAG "u_dump"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static Il2Cpp il;

// --- log C1 (mínimo da F3; migra pra mod_common.h na F2 sem mudar formato) ---
// files/bepinex/log.txt, append, 1 linha = HH:MM:SS [u_dump] msg, corta em 256KB.
static FILE *log_open_append(const char *pkg) {
    char dir[352], path[384];
    snprintf(dir, sizeof(dir), "/data/data/%s/files/bepinex", pkg);
    mkdir(dir, 0755);  // existe = ok; permissão vem do dono (o próprio app)
    snprintf(path, sizeof(path), "%s/log.txt", dir);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 256 * 1024) {
        FILE *f = fopen(path, "w");  // corta: contrato não fala de rotação
        if (f) fclose(f);
    }
    return fopen(path, "a");
}

static void c1_log(FILE *f, const char *msg) {
    LOG("%s", msg);
    if (!f) return;
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    fprintf(f, "%02d:%02d:%02d [u_dump] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msg);
    fflush(f);
}

// --- filesystem ---

// cmdline real do processo ("com.foo.app", 1º token).
static bool read_cmdline(char *out, size_t cap) {
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) return false;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    return n > 0 && out[0] != '\0';
}

// pkg do processo (C1): env do loader, senão cmdline fora de zygote* (retry:
// no constructor o cmdline ainda é zygote64 — achado em device com u_patch).
static bool resolve_pkg(char *pkg, size_t cap) {
    if (dump_pick_pkg(getenv("BEPINEX_PKG"), nullptr, pkg, cap)) return true;
    char cmd[256];
    for (int i = 0; i < 50; i++) {
        if (read_cmdline(cmd, sizeof(cmd)) && dump_pick_pkg(nullptr, cmd, pkg, cap))
            return true;
        usleep(200 * 1000);
    }
    return false;
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

// "Ns.Nome" num buffer. Aninhada: class_get_namespace vazio + nome curto
// ("Inner") — C5 não define formato pra aninhada (furo reportado ao
// orquestrador, não inventamos variante); C4 v1 ignora aninhada de qualquer
// forma.
static const char *full_class_name(void *klass, char *out, size_t cap) {
    const char *ns = il.class_get_namespace(klass);
    const char *nm = il.class_get_name(klass);
    if (!nm) { snprintf(out, cap, "?"); return out; }
    if (!ns || !ns[0]) { snprintf(out, cap, "%s", nm); return out; }
    snprintf(out, cap, "%s.%s", ns, nm);
    return out;
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

    // Métodos: iterador com cursor (void** estilo Java Enumeration).
    if (il.class_get_methods && il.method_get_name) {
        void *it = nullptr;
        while ((it = il.class_get_methods(klass, &it)) != nullptr) {
            uint32_t iflags = 0;
            uint32_t flags = il.method_get_flags ? il.method_get_flags(it, &iflags) : 0;
            const char *ret = type_name_of(il.method_get_return_type(it), line, sizeof(line));
            char mline[896];
            w = dump_write_method(mline, sizeof(mline), cls_name,
                                  il.method_get_name(it), il.method_get_param_count(it),
                                  ret, (flags & dump_attr_static_mask()) != 0);
            fwrite(mline, 1, (size_t)w, out);
            n++;
        }
    }
    // Campos: offset do field_get_offset (0 é offset real de estático/literal).
    if (il.class_get_fields && il.field_get_name) {
        void *it = nullptr;
        while ((it = il.class_get_fields(klass, &it)) != nullptr) {
            const char *nm = il.field_get_name(it);
            if (!nm) continue;
            int fflags = il.field_get_flags ? il.field_get_flags(it) : 0;
            char tbuf[512];
            const char *t = type_name_of(il.field_get_type(it), tbuf, sizeof(tbuf));
            char fline[1024];
            w = dump_write_field(fline, sizeof(fline), cls_name, nm, t,
                                 (fflags & (int)dump_attr_static_mask()) != 0,
                                 (int)il.field_get_offset(it));
            fwrite(fline, 1, (size_t)w, out);
            n++;
        }
    }
    return n;
}

static void *worker(void *) {
    char pkg[256], msg[256];
    if (!resolve_pkg(pkg, sizeof(pkg))) {
        LOG("pkg desconhecido (sem BEPINEX_PKG e cmdline zygote) — desistindo");
        return nullptr;
    }
    FILE *lg = log_open_append(pkg);

    char dir[352], path[384], tmp[448];
    snprintf(dir, sizeof(dir), "/data/data/%s/files/bepinex", pkg);
    snprintf(path, sizeof(path), "%s/dump.tsv", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    struct stat st;
    if (stat(path, &st) == 0) {
        snprintf(msg, sizeof(msg), "dump.tsv já existe (%lld bytes) — nada a fazer",
                 (long long)st.st_size);
        c1_log(lg, msg);
        if (lg) fclose(lg);
        return nullptr;
    }

    if (!il2cpp_boot(il)) {
        c1_log(lg, "il2cpp não subiu (timeout ~120s) — desistindo");
        if (lg) fclose(lg);
        return nullptr;
    }
    // Enumeração obrigatória (opcionais: flags/image_get_name, com fallback).
    if (!il.image_get_class_count || !il.image_get_class || !il.class_get_name ||
        !il.class_get_namespace || !il.type_get_name || !il.method_get_param_count ||
        !il.method_get_return_type || !il.class_get_methods || !il.class_get_fields) {
        c1_log(lg, "API de enumeração ausente nesta libil2cpp — desistindo");
        if (lg) fclose(lg);
        return nullptr;
    }

    FILE *out = fopen(tmp, "w");
    if (!out) {
        snprintf(msg, sizeof(msg), "não abriu %s (SELinux/perm?) — desistindo", tmp);
        c1_log(lg, msg);
        if (lg) fclose(lg);
        return nullptr;
    }

    size_t nasms = 0;
    void **asms = il.domain_get_assemblies(il.domain, &nasms);
    if (!asms || nasms == 0) {
        c1_log(lg, "nenhum assembly no domínio — desistindo");
        fclose(out);
        unlink(tmp);
        if (lg) fclose(lg);
        return nullptr;
    }

    {
        char hdr[512];
        int w = dump_write_header(hdr, sizeof(hdr), pkg, il2cpp_mapped_size(), "");
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
        c1_log(lg, msg);
        unlink(tmp);
        if (lg) fclose(lg);
        return nullptr;
    }
    snprintf(msg, sizeof(msg), "dump.tsv pronto: %lld linhas, %zu assemblies", total, nasms);
    c1_log(lg, msg);
    if (lg) fclose(lg);
    return nullptr;
}

__attribute__((constructor)) static void u_dump_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
