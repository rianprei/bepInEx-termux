// bc_signal.h — sinais entre o companion e o poll do loader.
//
// ACHADO REAL (device, Enforcing, v0.4.0): __system_property_set é NEGADO
// para o domínio do app (avc: denied { write } property_socket, permissive=0).
// O loader usava a property como mailbox: o companion sinalizava, o poll
// consumia e ZERAVA. Em Enforcing o reset nunca acontecia, o reload_config
// repetia a cada poll de 1s e o audit floodava. E o "reset" não podia existir:
// app nenhum escreve property.
//
// Novo desenho, sem nenhum set_property no processo do jogo:
//   - companion -> poll: memória do próprio processo. O companion roda NO
//     MESMO processo (connectCompanion), então property seria round-trip
//     Depois da curva e ainda assim negado nos dois lados.
//   - root externo (Termux su, Manager) -> poll: property, SÓ LEITURA. Root
//     escreve; o app lê e age quando o VALOR MUDA (seq), nunca quando o
//     valor é um flag que ele não pode limpar.
#ifndef BC_SIGNAL_H
#define BC_SIGNAL_H

#include <atomic>
#include <cstring>
#include <mutex>

// true = valor novo (não vazio e diferente do já visto) e memoriza em
// last_seen. Vazio não é pedido. last_seen é do chamador (n >= 1).
// Host-testável (test/selftest_harness.cpp).
static inline bool bc_seq_take(const char *cur, char *last_seen, size_t n) {
    if (cur == nullptr || cur[0] == '\0' || last_seen == nullptr || n == 0) return false;
    if (strcmp(cur, last_seen) == 0) return false;
    strncpy(last_seen, cur, n - 1);
    last_seen[n - 1] = '\0';
    return true;
}

// Pedido com nome (unpatch_mod/repatch_mod), in-process: o companion publica,
// o poll consome uma vez. O mutex cobre nome+seq juntos (senão o poll pode ver
// o nome de um pedido com a seq do anterior).
struct bc_named_req {
    std::mutex mu;
    char name[64] = {};
    unsigned seq = 0;

    void put(const char *n) {
        if (n == nullptr) return;
        std::lock_guard<std::mutex> l(mu);
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        seq++;
    }
    // true = chegou pedido novo; copia o nome e consome a seq.
    bool take(char *out, size_t outsz, unsigned *last_seq) {
        std::lock_guard<std::mutex> l(mu);
        if (seq == *last_seq) return false;
        strncpy(out, name, outsz - 1);
        out[outsz - 1] = '\0';
        *last_seq = seq;
        return true;
    }
};

// Sinais sem nome. 0 = nenhum pedido; o companion incrementa, o poll consome
// com exchange(0) (uma vez só, sem window perdido).
extern std::atomic<unsigned> g_sig_reload_config;  // reload_config
extern std::atomic<unsigned> g_sig_reload_mods;    // reload_mods
extern std::atomic<unsigned> g_sig_patches_req;    // list_patches (valor = seq do pedido)
extern bc_named_req g_sig_unpatch;                 // unpatch_mod <mod>
extern bc_named_req g_sig_repatch;                 // repatch_mod <mod>

#endif // BC_SIGNAL_H
