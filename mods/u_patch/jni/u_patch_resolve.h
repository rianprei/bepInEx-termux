// u_patch_resolve.h — resolução de tipo do campo, COM CHECK em cada elo.
//
// Núcleo puro, sem Android: o teste host
// (mods/u_patch/jni/u_field_nresolve_test.cpp, Caso 84) monta um Il2Cpp FALSO
// que devolve nullptr em cada ponto da cadeia e exige que nenhum elo crasse —
// e que cada um diga POR QUE recusou. Antes este cabeçalho citava o caso do
// scan do loader e um caminho de teste que não existe.
//
// ACHADO REAL (device POCO C75, SA2, 2026-09-27): a regra
// `field WeaponInfo unlimitedAmmo bool true` derrubou o jogo.
// Tombstone_07_f4field: SIGSEGV, fault addr 0x135, x0 = 0, dentro de
// il2cpp_type_get_name+24, chamado de u_patch.so+0x1bb34. Simbolizando o
// offset contra o build da base (5ed8019):
//
//   #04 u_patch.so+0x1bb34 → up_field_type_name  u_patch_mod.cpp:375
//   #05 u_patch.so+0x1a5f0 → up_apply_field       u_patch_mod.cpp:523
//
// A linha 375 era `il.type_get_name(k)`, com `k` = o Il2CppClass* devolvido
// por il2cpp_class_from_type. Mas il2cpp_type_get_name recebe um Il2CppType*.
// Passar a classe é TYPE CONFUSION: o runtime caminha a Il2CppClass como se
// fosse Il2CppType e desreferencia um campo que ele lê como ponteiro — daí o
// x0 = 0 e o fault longe do início do objeto. O class_from_type serve só como
// PROVA de que o tipo resolve; quem vai pro type_get_name é o TYPE.
//
// A referência do contrato está em mods/u_dump/jni/u_dump_mod.cpp
// (type_name_of): passa o type direto, sem passar por class_from_type.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "../../common/il2cpp_min.h"

// Motivo da recusa. UP_RS_OK é o único caminho que devolve nome; os outros
// são exatamente os elos da cadeia que podem devolver nullptr.
typedef enum {
    UP_RS_OK = 0,
    UP_RS_NO_API,        // il2cpp sem field_get_type/class_from_type/type_get_name
    UP_RS_NO_FIELD,      // o FieldInfo chegou nulo
    UP_RS_NO_TYPE,       // il2cpp_field_get_type devolveu nulo
    UP_RS_NOT_CLASS,     // il2cpp_class_from_type devolveu nulo (VAR/MVAR/byref/ponteiro)
    UP_RS_NO_NAME,       // il2cpp_type_get_name devolveu nulo
    UP_RS_TOO_LONG,      // o nome não cabia em outsz (truncado = recusado)
    UP_RS_NO_FREE,       // il2cpp_free ausente: o nome é do runtime, não dá pra soltar
} up_resolve_status;

// Texto do motivo, para o log. Estável (vira contrato do teste host).
static inline const char *up_resolve_reason(up_resolve_status st) {
    switch (st) {
        case UP_RS_OK: return "ok";
        case UP_RS_NO_API: return "il2cpp sem a API de tipo";
        case UP_RS_NO_FIELD: return "campo não resolvido";
        case UP_RS_NO_TYPE: return "campo sem tipo";
        case UP_RS_NOT_CLASS: return "tipo não é classe (genérico/ponteiro/byref)";
        case UP_RS_NO_NAME: return "tipo sem nome";
        case UP_RS_TOO_LONG: return "nome do tipo maior que o buffer";
        case UP_RS_NO_FREE: return "il2cpp sem free (nome do runtime não pode ser copiado)";
    }
    return "?";
}

// Nome do tipo de um campo, com TUDO checado. 0 = `out` preenchido.
//
// Regra do tipo confusion, em uma linha: o que vai para type_get_name é
// SEMPRE o Il2CppType* (`t`), nunca a classe. `class_from_type` é só a prova
// de que o tipo resolve para uma classe — e o resultado é DESCARTADO de
// propósito, para o teste não ter como reintroduzir o bug sem perceber.
static inline up_resolve_status up_resolve_field_type(const Il2Cpp *il, void *field,
                                                      char *out, size_t outsz) {
    if (il == nullptr || out == nullptr || outsz == 0) return UP_RS_NO_API;
    if (!il->field_get_type || !il->class_from_type || !il->type_get_name) return UP_RS_NO_API;
    if (field == nullptr) return UP_RS_NO_FIELD;
    const void *t = il->field_get_type(field);
    if (t == nullptr) return UP_RS_NO_TYPE;
    // Prova de resolutabilidade. O valor NÃO é usado adiante.
    if (il->class_from_type((void *)t) == nullptr) return UP_RS_NOT_CLASS;
    char *n = il->type_get_name(t);   // <-- o TYPE, nunca a classe
    if (n == nullptr) return UP_RS_NO_NAME;
    size_t len = strnlen(n, outsz);
    if (len >= outsz) {
        // Não cabe: recusa em vez de truncar, porque um nome truncado
        // ("System.Bool") não casa com nada e o chamador acharia que o tipo
        // é desconhecido.
        if (il->free) il->free(n);
        return UP_RS_TOO_LONG;
    }
    // il2cpp_type_get_name aloca com o ALOCADOR DO RUNTIME, e quem devolve é
    // il2cpp_free. Sem ele, o nome é uma ponteira que NINGUÉM pode soltar com
    // segurança: o `free()` da libc seria liberar memória do il2cpp, que
    // destrói o heap do jogo (achado no teste host, ver Caso 87 — "free
    // ausente: recusa em vez de free() da libc"; o número anterior apontava
    // para o caso de wildcard do pattern scan). E como não dá
    // pra copiar sem devolver, a recusa é a resposta honesta — o chamador
    // logou e pula a regra. Um il2cpp sem il2cpp_free é de uma versão
    // onde isto não existia; melhor uma regra a menos do que heap.
    if (!il->free) return UP_RS_NO_FREE;
    memcpy(out, n, len + 1);
    il->free(n);
    return UP_RS_OK;
}

// Versão com o motivo pronto pro log, no formato que o mod escreve:
// "campo <cls>::<membro> não encontrado em <img> — patch ignorado" quando o
// elo que falhou é a resolução do tipo, e a frase específica do motivo logo
// depois. Uma linha só, PT-BR, porque o usuário lê isso no log.txt do device.
static inline void up_resolve_log_line(up_resolve_status st, const char *cls,
                                       const char *member, char *out, size_t outsz) {
    if (out == nullptr || outsz == 0) return;
    snprintf(out, outsz, "campo %s::%s não encontrado em %s — patch ignorado (%s)",
             cls ? cls : "?", member ? member : "?", cls ? cls : "?", up_resolve_reason(st));
}
