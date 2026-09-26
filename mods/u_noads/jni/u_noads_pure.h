// u_noads_pure.h — parte pura do u_noads (host-testável, sem il2cpp):
// split de nome de classe, guarda de método curto e contagem do pool.
// A guarda usa o mesmo critério do u_patch: Dobby arm64 escreve ~16 bytes
// (4 palavras) no prólogo; se qualquer palavra sobrescrita já é ret/br/B,
// o método é mais curto que o trampolim e o hook vazaria pro vizinho.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// "Ns.Classe" -> ns + nome; sem ponto (ex.: "MaxSdk") -> ns vazia.
// Aninhada C# ("A/B" do C5) não entra aqui: nested se resolve por
// class_get_nested_types, não por string.
static inline void uno_split(const char *fqn, char *ns, size_t nsz, char *nm, size_t nmsz) {
    if (!fqn) { if (nsz) ns[0] = '\0'; if (nmsz) nm[0] = '\0'; return; }
    const char *dot = strrchr(fqn, '.');
    if (!dot) {
        if (nsz) ns[0] = '\0';
        snprintf(nm, nmsz, "%s", fqn);
        return;
    }
    size_t nlen = (size_t)(dot - fqn);
    if (nlen >= nsz) nlen = nsz - 1;
    memcpy(ns, fqn, nlen);
    ns[nlen] = '\0';
    snprintf(nm, nmsz, "%s", dot + 1);
}

static inline bool uno_is_terminator(uint32_t w) {
    if ((w & 0xFFFFFC1Fu) == 0xD65F0000u) return true;  // ret
    if ((w & 0xFFFFFC1Fu) == 0xD61F0000u) return true;  // br
    if ((w & 0xFC000000u) == 0x14000000u) return true;  // b
    return false;
}

// Dobby arm64 escreve ~16 bytes (4 palavras) no prólogo. Checa [0..2]:
// a 4ª palavra vira o fim do trampolim, então um ret exatamente ali é ok
// (método de exatos 16 bytes).
static inline bool uno_method_fits(const uint32_t *orig) {
    if (!orig) return false;
    for (int i = 0; i < 3; i++)
        if (uno_is_terminator(orig[i])) return false;
    return true;
}
