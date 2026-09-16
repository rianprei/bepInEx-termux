// Symbol Scanning Fallback para Zygisk BC POC
//
// IDEIA:
// Em vez de confiar apenas em DobbySymbolResolver (que falha se símbolo
// for renomeado), implementamos fallback de scanning por padrão de bytes.
//
// FLUXO:
// 1. Tenta DobbySymbolResolver primeiro (rápido, confiável)
// 2. Se falhar, verifica se endereço retornado tem assinatura correta
// 3. Se assinatura não bater, scaneia .so procurando por padrão de bytes
// 4. Retorna endereço encontrado ou nullptr se falhar
//
// VANTAGENS:
// - Funciona se função for renomeada
// - Funciona se package mudar
// - Mais resistente a updates do jogo
//
// LIMITAÇÕES:
// - Signature precisa ser extraída do binário específico
// - Pode mudar entre compilações (otimizações do compilador)
// - Falso-positivos possíveis se pattern for muito genérico
//
// NOTA: Este é ESBOÇO - não 100% funcional ainda.
// Signature de bytes abaixo é PLACEHOLDER e precisa ser extraída
// via análise do binário real do Battle Cats (readelf, objdump, Ghidra).

#include <android/log.h>
#include <stdint.h>
#include <link.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define LOG_TAG "BCPOC_SCAN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================================
// Funções Helper: Obter base address e tamanho da lib
// ============================================================================

// Estrutura para contexto de busca de base address
struct LibBaseCtx {
    const char *libname;
    void *base;
};

// Callback para dl_iterate_phdr - encontra base address da lib
static int find_lib_base(struct dl_phdr_info *info, size_t, void *data) {
    auto *ctx = static_cast<LibBaseCtx *>(data);
    if (info->dlpi_name && strstr(info->dlpi_name, ctx->libname)) {
        ctx->base = (void *)info->dlpi_addr;
        return 1;  // Stop iteration
    }
    return 0;
}

// Obtém base address da lib no espaço de endereço do processo
static void *get_lib_base(const char *libname) {
    LibBaseCtx ctx{libname, nullptr};
    dl_iterate_phdr(find_lib_base, &ctx);
    return ctx.base;
}

// Obtém tamanho da lib lendo /proc/self/maps
static size_t get_lib_size(const char *libname) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("failed to open /proc/self/maps");
        return 0;
    }

    char line[512];
    size_t size = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libname)) {
            unsigned long start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
                size = end - start;
                LOGI("lib %s size: %zu bytes (0x%zx)", libname, size, size);
                break;
            }
        }
    }
    fclose(fp);
    return size;
}

// ============================================================================
// Verificação de Assinatura de Bytes
// ============================================================================

// Estrutura para definir uma signature de bytes
struct ByteSignature {
    const uint8_t *bytes;      // Array de bytes
    size_t length;             // Tamanho da signature
    const char *description;   // Descrição para debug
};

// Verifica se os bytes em um endereço correspondem à signature
// 0xFF em signature = wildcard (qualquer byte)
static bool check_signature(void *addr, const ByteSignature *sig) {
    if (!addr || !sig || !sig->bytes) return false;

    uint8_t *mem = static_cast<uint8_t *>(addr);
    for (size_t i = 0; i < sig->length; i++) {
        uint8_t expected = sig->bytes[i];
        if (expected != 0xFF && mem[i] != expected) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Signature de Bytes do JNI Target (PLACEHOLDER)
// ============================================================================

// NOTA CRÍTICA: Esta signature é PLACEHOLDER e NÃO FUNCIONARÁ no binário real.
//
// Para extrair a signature correta:
// 1. Extraia libnative-lib.so do APK (unzip)
// 2. Abra no Ghidra/IDA/Binary Ninja
// 3. Encontre a função Java_jp_co_ponos_battlecats_MyActivity_appUpdateDraw
// 4. Copie os primeiros 16-32 bytes da função (prólogo)
// 5. Substitua o array abaixo com os bytes reais
//
// Exemplo de prólogo JNI típico em AArch64:
// - stp x29, x30, [sp, #-16]!  -> 0xF0 0x4F 0xBE 0xA9
// - mov x29, sp                -> 0xFD 0x03 0x00 0x91
// - stp x19, x20, [sp, #-16]!  -> 0xF8 0x47 0xBE 0xA9
// ...

static const uint8_t JNI_FRAME_SIGNATURE[] = {
    0xF0, 0x4F, 0xBE, 0xA9,  // stp x29, x30, [sp, #-16]!  (TÍPICO JNI)
    0xFD, 0x03, 0x00, 0x91,  // mov x29, sp
    0xFF, 0xFF, 0xFF, 0xFF,  // Placeholder - precisa preencher
    0xFF, 0xFF, 0xFF, 0xFF,  // Placeholder - precisa preencher
    0xFF, 0xFF, 0xFF, 0xFF,  // Placeholder - precisa preencher
    0xFF, 0xFF, 0xFF, 0xFF,  // Placeholder - precisa preencher
};

static const ByteSignature JNI_FRAME_SIG = {
    JNI_FRAME_SIGNATURE,
    sizeof(JNI_FRAME_SIGNATURE),
    "JNI frame update function prologue"
};

// ============================================================================
// Symbol Scanning Principal
// ============================================================================

// Scaneia a memória da lib procurando pela signature
// Retorna endereço encontrado ou nullptr
static void *scan_for_signature(const char *libname, const ByteSignature *sig) {
    void *base = get_lib_base(libname);
    if (!base) {
        LOGE("scan_for_signature: failed to get lib base for %s", libname);
        return nullptr;
    }

    size_t size = get_lib_size(libname);
    if (size == 0) {
        LOGE("scan_for_signature: failed to get lib size for %s", libname);
        return nullptr;
    }

    if (size < sig->length) {
        LOGE("scan_for_signature: lib size %zu < signature length %zu", size, sig->length);
        return nullptr;
    }

    LOGI("scan_for_signature: scanning %s (base=%p, size=%zu) for %s",
         libname, base, size, sig->description);

    uint8_t *mem = static_cast<uint8_t *>(base);
    size_t sig_len = sig->length;

    // Scanneia byte a byte (pode ser otimizado com memchr/boyer-moore)
    for (size_t i = 0; i < size - sig_len; i++) {
        bool match = true;
        for (size_t j = 0; j < sig_len; j++) {
            uint8_t expected = sig->bytes[j];
            if (expected != 0xFF && mem[i + j] != expected) {
                match = false;
                break;
            }
        }

        if (match) {
            LOGI("scan_for_signature: found match at offset %zx (addr=%p)", i, mem + i);
            return mem + i;
        }
    }

    LOGE("scan_for_signature: no match found for %s", sig->description);
    return nullptr;
}

// ============================================================================
// Integração com Código Atual (Fallback Logic)
// ============================================================================

// Função principal que tenta symbol resolution primeiro, depois scanning
// Esta é a função que seria chamada do main.cpp
void *resolve_jni_symbol_with_fallback(const char *libname, const char *sym_name) {
    // PASSO 1: Tenta DobbySymbolResolver (método primário)
    extern void *DobbySymbolResolver(const char *, const char *);
    void *sym = DobbySymbolResolver(libname, sym_name);

    if (sym) {
        LOGI("resolve_jni_symbol_with_fallback: symbol resolution succeeded: %s", sym_name);

        // PASSO 2: Verifica se o endereço tem a signature correta
        // (útil para detectar se o símbolo mudou ou foi ofuscado)
        if (check_signature(sym, &JNI_FRAME_SIG)) {
            LOGI("resolve_jni_symbol_with_fallback: signature check passed");
            return sym;
        } else {
            LOGW("resolve_jni_symbol_with_fallback: symbol found but signature mismatch");
            LOGW("resolve_jni_symbol_with_fallback: possible symbol rename/obfuscation");
            // Continua para scanning fallback
        }
    } else {
        LOGI("resolve_jni_symbol_with_fallback: symbol resolution failed, trying scan");
    }

    // PASSO 3: Fallback - symbol scanning
    void *scanned = scan_for_signature(libname, &JNI_FRAME_SIG);
    if (scanned) {
        LOGI("resolve_jni_symbol_with_fallback: signature scan succeeded (fallback)");
        return scanned;
    }

    // PASSO 4: Falha total
    LOGE("resolve_jni_symbol_with_fallback: neither resolution nor scanning succeeded");
    return nullptr;
}

// ============================================================================
// Exemplo de Uso (como integrar com main.cpp)
// ============================================================================

/*
// Em main.cpp, substituir a linha atual:
// void *sym = DobbySymbolResolver(TARGET_LIB, HOOK_SYM);

// Por:
#include "symbol_scan.cpp"  // ou declarar função em header

static void install_intercept_probe() {
    // Usa fallback logic
    void *sym = resolve_jni_symbol_with_fallback(TARGET_LIB, HOOK_SYM);

    if (!sym) {
        LOGE("install_intercept_probe: failed to resolve symbol even with fallback");
        return;
    }

    int rc = DobbyHook(sym, (void *)my_appUpdateDraw, (void **)&orig_fn);
    if (rc == 0) {
        LOGI("hook instalado em %s (via %s) — interceptação ativa", HOOK_SYM, TARGET_LIB);
    } else {
        LOGE("DobbyHook rc=%d", rc);
        orig_fn = nullptr;  // IMPORTANTE: zerar para evitar use-after-free
    }
}
*/

// ============================================================================
// TODO: Implementação Completa Requer
// ============================================================================

/*
 * [ ] 1. Extrair signature real do binário do Battle Cats
 *     - Ferramentas: readelf, objdump, Ghidra, IDA Pro
 *     - Target: Java_jp_co_ponos_battlecats_MyActivity_appUpdateDraw
 *     - Copiar primeiros 16-32 bytes (prólogo da função)
 *
 * [ ] 2. Testar signature em múltiplas versões do APK
 *     - Battle Cats EN 15.5.0
 *     - Battle Cats EN 15.6.0
 *     - Battle Cats EN 15.7.0
 *     - Verificar se signature é estável entre versões
 *
 * [ ] 3. Otimizar scanning (se necessário)
 *     - Usar memchr para primeiro byte
 *     - Implementar Boyer-Moore para patterns longos
 *     - Paralelizar scanning se lib for muito grande
 *
 * [ ] 4. Adicionar heurística para reduzir falso-positivos
 *     - Verificar se endereço está em seção .text
 *     - Verificar se endereço está alinhado (4 bytes)
 *     - Verificar se endereço está próximo de outras funções JNI
 *
 * [ ] 5. Adicionar fallback adicional (se necessário)
 *     - Scan por referência cruzada (xref)
 *     - Scan por padrão de string table
 *     - Scan por pattern de chamada de função
 *
 * [ ] 6. Adicionar cache de resultados
 *     - Se já scaneou uma vez, não scanear novamente
 *     - Cache por (libname, version) para evitar re-scan
 *
 * [ ] 7. Adicionar logging detalhado para debug
 *     - Log cada offset scaneado (verbose mode)
 *     - Log cada match potencial (com contexto)
 *     - Log estatísticas (tempo de scan, bytes scaneados)
 */
