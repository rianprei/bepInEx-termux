// bc_signal.h — contrato dos sinais entre o companion (processo separado, root)
// e o jogo (app domain).
//
// ACHADO REAL (device, Enforcing, v0.4.0): o jogo fazia
// __system_property_set para limpar o sinal que tinha acabado de ler, e
// app domain é NEGADO (avc: denied { write } property_socket). O clear nunca
// acontecia: reload_config repetia a cada poll de 1s e o audit floodava.
//
// Os dois lados NÃO dividem memória: companion_handler() é chamado pelo daemon
// zygiskd (ver o comentário em companion.cpp), e o "companion process" é um
// daemon separado (double-fork, root). Então:
//
//   root -> app: property, o root ESCREVE (pode), o app SÓ LÊ. O app age
//     quando o VALOR muda, nunca quando um flag está ligado — não existe mais
//     como o app limpar flag. Por isso os valores são CONTADORES de seq: o
//     mesmo comando pode ser repetido e o app reage uma vez por mudança.
//   app -> root: o app não escreve property nem /data/local/tmp (negado em
//     Enforcing). Resposta vai para arquivo no diretório C1 do jogo
//     (/data/data/<pkg>/files/bepinex/), que o companion lê como root.
//
// Payload nomeado (unpatch/repatch): "`<seq> <nome>`" — o nome não tem espaço
// (é nome de arquivo, validado com strlen < PROP_VALUE_MAX), então o primeiro
// espaço separa as duas metades.
#ifndef BC_SIGNAL_H
#define BC_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Properties (uma definição só, usada pelos dois lados).
#define BC_PROP_RELOAD_CONFIG "persist.bc_poc.reload_config"
#define BC_PROP_RELOAD_MODS "persist.bc_poc.reload_mods"
#define BC_PROP_UNPATCH "persist.bc_poc.unpatch_target"
#define BC_PROP_REPATCH "persist.bc_poc.repatch_target"
#define BC_PROP_PATCHES_REQ "persist.bc_poc.patches_req"

// Respostas do jogo para o companion (contrato C1). O caminho BC tem pkg fixo
// (o caminho BC só roda no Battle Cats); o companion e o jogo concordam porque
// a constante é esta.
#define BC_STATE_DIR "/data/data/jp.co.ponos.battlecatsen/files/bepinex"
#define BC_OVERHEAD_FILE BC_STATE_DIR "/bc_hook_overhead_us"
#define BC_PATCHES_FILE BC_STATE_DIR "/bc_patches.txt"

// true = valor novo (não vazio e diferente do já visto) e memoriza em
// last_seen. Vazio não é pedido. Host-testável (selftest_harness caso 59).
static inline bool bc_seq_take(const char *cur, char *last_seen, size_t n) {
    if (cur == nullptr || cur[0] == '\0' || last_seen == nullptr || n == 0) return false;
    if (strcmp(cur, last_seen) == 0) return false;
    strncpy(last_seen, cur, n - 1);
    last_seen[n - 1] = '\0';
    return true;
}

// Quebra "<seq> <payload>": true quando tem as duas metades. Sem espaço,
// devolve false (o chamador loga e ignora) — nunca adivinha.
static inline bool bc_seq_split(const char *v, char *key, size_t ksz, char *payload,
                                size_t psz) {
    if (v == nullptr || key == nullptr || payload == nullptr || ksz == 0 || psz == 0) return false;
    const char *sp = strchr(v, ' ');
    if (sp == nullptr || sp == v) return false;
    size_t klen = (size_t)(sp - v);
    if (klen >= ksz) return false;
    memcpy(key, v, klen);
    key[klen] = '\0';
    snprintf(payload, psz, "%s", sp + 1);
    return payload[0] != '\0';
}

#endif // BC_SIGNAL_H
