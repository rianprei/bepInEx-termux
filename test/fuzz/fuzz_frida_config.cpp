// fuzz_frida_config — alvo de fuzzing de uf_config_is_script_mode e dos
// vizinhos puros do u_frida (mods/u_frida/jni/u_frida_config.h).
//
// Por que importa: o config do frida-gadget é um ARQUIVO que o usuário
// escreve no diretório de mods e o loader nativo lê DENTRO do processo do
// jogo. O default do gadget é ListenInteraction com on_load=wait: config
// `{}`, sem "interaction", ou type listen/connect CONGELA o jogo esperando
// um cliente e abre socket. Então o validador é a barreira que decide se o
// gadget entra; um bug aqui é jogo travado, não só "mod não carregou".
//
// O fuzzer mexe em toda a superfície de string do header:
//   - uf_config_is_script_mode(buf, n, why, whysz) com n = tamanho real do
//     input e o input copiado SEM NUL final (o arquivo é lido por tamanho,
//     não como string C) — por isso o memchr('\0', n) interno;
//   - uf_is_js_mod / uf_pkg_ok, que recebem nome de arquivo e nome de pacote
//     vindos do usuário e decidem o que o loader vai abrir;
//   - uf_build_config, que monta o config com um moddir vindo do ambiente.
//
// O `why` é sempre chamado com o MESMO tamanho do buffer que o chamador real
// usa (64 no u_frida_mod.cpp), e uma segunda chamada com why == nullptr
// (o caminho de quem não loga) — os dois precisam ser seguros.
//
// Compilar:
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//           -I../../mods/u_frida/jni fuzz_frida_config.cpp -o fuzz_frida_config

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../../mods/u_frida/jni/u_frida_config.h"

namespace {

constexpr size_t kMaxInput = 64 * 1024;

// O `why` é sempre chamado com o MESMO tamanho do buffer que o chamador real
// usa (64 no u_frida_mod.cpp), e uma segunda chamada com why == nullptr
// (o caminho de quem não loga) — os dois precisam ser seguros.
//
// ALOCAÇÃO EXATA, e isso é o ponto: o loader real entrega um buffer do
// TAMANHO DO ARQUIVO, e o validador promete respeitar `n`. Se o harness
// usasse um buffer global de 64 KB, uma leitura 1 byte além de `n` cairia
// dentro do array e o ASan não veria nada — que é exatamente a classe de bug
// que um validador de JSON tem ("leu o byte depois do fim?"). Alocando
// `size` bytes, qualquer leitura além de `n` é heap-buffer-overflow e o
// sanitizer pega.
struct Exact {
    char *p;
    size_t n;
};

Exact alloc_exact(const uint8_t *data, size_t size) {
    Exact e = {nullptr, 0};
    if (size == 0) return e;
    e.p = (char *)malloc(size);
    if (e.p == nullptr) return e;
    memcpy(e.p, data, size);
    e.n = size;
    return e;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > kMaxInput) size = kMaxInput;

    char why[64];

    // (a) contrato de TAMANHO: buffer com `size` bytes, sem NUL. É assim que
    //     o loader entrega o arquivo (read() + n). Qualquer leitura além de
    //     size aqui é OOB de verdade e o ASan acusa.
    {
        Exact e = alloc_exact(data, size);
        if (e.p != nullptr) {
            const bool ok = uf_config_is_script_mode(e.p, e.n, why, sizeof(why));
            // Mesmo input, sem log: o caminho de quem passa why == nullptr.
            (void)uf_config_is_script_mode(e.p, e.n, nullptr, 0);
            // `why` de 1 byte: snprintf com %.*s de um type longo tem que caber.
            char one[1];
            (void)uf_config_is_script_mode(e.p, e.n, one, sizeof(one));
            // O `type` devolvido é usado pelo mod pra decidir script vs
            // script-directory; uma leitura além de n aqui seria o bug.
            if (ok) (void)why[0];
            free(e.p);
        }
    }

    // (b) contrato de STRING: o mesmo input NUL-terminado (o outro chamador
    //     possível). Sem NUL o validador recusa de cara por design, então as
    //     DUAS variantes são necessárias: só (a) deixaria o `memchr('\0', n)`
    //     interno Short-circuitar tudo.
    {
        char *s = (char *)malloc(size + 1);
        if (s != nullptr) {
            if (size) memcpy(s, data, size);
            s[size] = '\0';
            (void)uf_config_is_script_mode(s, size, why, sizeof(why));
            free(s);
        }
    }

    // Vizinhos: nome de arquivo de mod (.js) e nome de pacote. Ambos
    // decidem o que o loader abre/escreve como root.
    {
        char name[kMaxInput + 1];
        memcpy(name, data, size);
        name[size] = '\0';
        (void)uf_is_js_mod(name);
        (void)uf_pkg_ok(name);
    }

    // Montagem do config com moddir vindo do ambiente do jogo.
    {
        char moddir[kMaxInput + 1];
        memcpy(moddir, data, size);
        moddir[size] = '\0';
        char out[512];
        int w = uf_build_config(out, sizeof(out), moddir);
        if (w > 0) {
            // O config que acabou de ser montado tem que passar no validador
            // (círculo fechado: build -> validate).
            size_t m = (size_t)w < sizeof(out) ? (size_t)w : sizeof(out) - 1;
            (void)uf_config_is_script_mode(out, m, why, sizeof(why));
        }
    }

    return 0;
}
