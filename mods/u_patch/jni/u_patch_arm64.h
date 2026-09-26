// u_patch_arm64.h — emissores de patch arm64 do u_patch. Header-only e puro
// (sem Android/il2cpp): o teste host confere palavra por palavra.
// Convenções: return sempre escreve x0 (cobre bool/int/long) ou s0 (float);
// mul usa thunk próprio (x0 ou s0) que preserva SP e registradores de args.
#pragma once
#include <cstdint>
#include <cstddef>

// Offsets no slot de dados de cada thunk mul (memória normal, .bss).
#define UP_SLOT_ORIG 0    // void* trampolim do Dobby (orig)
#define UP_SLOT_VALUE 8   // int64 ou bits do float nos 32 baixos
#define UP_SLOT_LR 16     // x30 salvo (SP nunca mexe — stack args intactos)
#define UP_SLOT_LOCK 24   // spinlock (reentrância vira chamada direta, sem travar)
#define UP_SLOT_SIZE 32

// Opcodes fixos (codificação conferida contra llvm-objdump do NDK — Caso 57).
#define UP_RET 0xD65F03C0           // ret
#define UP_BLR_X16 0xD63F0200       // blr x16
#define UP_LDR_X16_ORIG 0xF9400210  // ldr x16, [x16]
#define UP_MUL_X0 0x9B017C00        // mul x0, x0, x1
#define UP_FMUL_S0 0x1E210800       // fmul s0, s0, s1
#define UP_FMOV_S0_W9 0x1E270120    // fmov s0, w9
#define UP_MOVZ_W9_1 0x52800029     // mov w9, #1
// x18 é reservado da plataforma no Android (ShadowCallStack): o thunk só
// usa x9/x10 (temporários caller-saved, livres na entrada — args vão em
// x0-x7 e o chamador não espera x9-x15 vivos depois do bl).

// movz x0, #imm16
static inline uint32_t up_enc_movz_x0(uint16_t imm) {
    return 0xD2800000u | ((uint32_t)imm << 5);
}
// movk x0, #imm16, lsl #16
static inline uint32_t up_enc_movk_x0_16(uint16_t imm) {
    return 0xF2A00000u | ((uint32_t)imm << 5);
}
// movz w9, #imm16
static inline uint32_t up_enc_movz_w9(uint16_t imm) {
    return 0x52800000u | ((uint32_t)imm << 5) | 9u;
}
// movk w9, #imm16, lsl #16
static inline uint32_t up_enc_movk_w9_16(uint16_t imm) {
    return 0x72A00000u | ((uint32_t)imm << 5) | 9u;
}
// adrp x16, <página de target> (pc = endereço da instrução)
static inline uint32_t up_enc_adrp_x16(const void *pc, const void *target) {
    int64_t off = ((int64_t)(uintptr_t)target >> 12) - ((int64_t)(uintptr_t)pc >> 12);
    uint32_t o = (uint32_t)(off & 0x1FFFFF);
    return 0x90000010u | ((o & 3u) << 29) | (((o >> 2) & 0x7FFFFu) << 5);
}
// add x16, x16, #lo12 (lo12 < 4096)
static inline uint32_t up_enc_add_x16(uint32_t lo12) {
    return 0x91000210u | ((lo12 & 0xFFFu) << 10);
}
// LDXR/STXR NÃO têm imediato de offset (exclusivas só endereço base).
// Lock: x17 = x16 + 24 no thunk (adrp+add), então endereça [x17] direto.
// ldxr w9, [x17]
static inline uint32_t up_enc_ldxr_lock() {
    return 0x885F7E29u;
}
// stxr w10, w9, [x17] (Rs=w10 grava status, Rt=w9 é o dado)
static inline uint32_t up_enc_stxr_lock() {
    return 0x880A7E29u;
}
// add x17, x16, #imm — endereço do lock vem de x16 (que já aponta pro
// slot): UMA instrução, register-relative, sem risco de lo12 embrulhar
// página. Rn=16 (x18 é reservado da plataforma — Rn=18 leria endereço
// selvagem; decodificação de campo no harness pega isso).
static inline uint32_t up_enc_add_x17_imm(uint32_t imm) {
    return 0x91000000u | ((imm & 0xFFFu) << 10) | (16u << 5) | 17u;
}
// cbnz wR, para a palavra (this_idx -> target_idx)
static inline uint32_t up_enc_cbnz_w(uint32_t r, int this_idx, int target_idx) {
    int32_t off = (target_idx - this_idx) * 4;
    return 0x35000000u | (((uint32_t)(off >> 2) & 0x7FFFFu) << 5) | (r & 31u);
}
// str xR, [x16, #off] (off múltiplo de 8)
static inline uint32_t up_enc_str_x(uint32_t r, uint32_t off) {
    return 0xF9000000u | (((off >> 3) & 0xFFFu) << 10) | (16u << 5) | (r & 31u);
}
// ldr xR, [x16, #off] (off múltiplo de 8)
static inline uint32_t up_enc_ldr_x(uint32_t r, uint32_t off) {
    return 0xF9400000u | (((off >> 3) & 0xFFFu) << 10) | (16u << 5) | (r & 31u);
}
// ldr s1, [x16, #8]
static inline uint32_t up_enc_ldr_s1() {
    return 0xBD400A01u;
}

// return bool: movz x0, #v; ret — 2 palavras (8 bytes).
static inline int up_emit_return_bool(uint32_t *out, int v) {
    out[0] = up_enc_movz_x0((uint16_t)(v ? 1 : 0));
    out[1] = UP_RET;
    return 2;
}
// return int (32 bits): movz + [movk lsl#16 se alto != 0] + ret — 2 ou 3 palavras.
static inline int up_emit_return_int(uint32_t *out, uint32_t v) {
    out[0] = up_enc_movz_x0((uint16_t)(v & 0xFFFFu));
    int n = 1;
    if (v >> 16) out[n++] = up_enc_movk_x0_16((uint16_t)(v >> 16));
    out[n++] = UP_RET;
    return n;
}
// return float: movz/movk w9 com os bits + fmov s0, w9 + ret — 4 palavras.
static inline int up_emit_return_float(uint32_t *out, uint32_t bits) {
    out[0] = up_enc_movz_w9((uint16_t)(bits & 0xFFFFu));
    out[1] = up_enc_movk_w9_16((uint16_t)(bits >> 16));
    out[2] = UP_FMOV_S0_W9;
    out[3] = UP_RET;
    return 4;
}

// Instrução que termina a função: ret/br (família 0xD61F/0xD65F) ou B
// incondicional. Se uma aparece antes da última palavra que vamos
// sobrescrever, o método é mais curto que o patch (ex.: getter de 8 bytes
// com return float de 16) e escrever vazaria pro método seguinte.
static inline bool up_is_terminator(uint32_t w) {
    if ((w & 0xFFFFFC1Fu) == 0xD65F0000u) return true;  // ret
    if ((w & 0xFFFFFC1Fu) == 0xD61F0000u) return true;  // br
    if ((w & 0xFC000000u) == 0x14000000u) return true;  // b
    return false;
}

// true = dá pra escrever nwords in-place. Só olha [0..n-2]: a última
// palavra vira o nosso ret, então um ret original exatamente ali é ok.
static inline bool up_method_fits(const uint32_t *orig, int nwords) {
    if (!orig || nwords < 2) return false;
    for (int i = 0; i < nwords - 1; i++)
        if (up_is_terminator(orig[i])) return false;
    return true;
}

// Thunk mul: chama orig com regs intactos, multiplica o retorno.
// R0-R7/stack preservados (só x16/x17/x1 mexidos pós-chamada, todos
// caller-saved; x18 é reservado da plataforma, nunca usado). Trava curta:
// reentrância cai no caminho direto (sem mul, sem hang).
// Layout (21 palavras): 0-1 adrp/add x16=slot, 2 add x17,x16,#24 (lock;
// LDXR/STXR não têm imediato), 3 ldxr w9,[x17], 4 cbnz ocupado->direto,
// 5 mov w9,#1, 6 stxr w10,w9,[x17], 7 cbnz perdeu->ldxr, 8 str lr,
// 9 ldr orig, 10 blr, 11-12 adrp/add x16=slot, 13 ldr fator, 14 mul,
// 15 str xzr unlock, 16 restore lr, 17 ret, 18-20 caminho direto.
static inline int up_emit_mul_thunk(uint32_t *out, const void *thunk_va, const void *slot_va, bool is_float) {
    uintptr_t slot = (uintptr_t)slot_va;
    uint32_t lo = (uint32_t)(slot & 0xFFFu);
    auto at = [&](int i) -> const void * { return (const uint8_t *)thunk_va + (size_t)i * 4; };
    int i = 0;
    out[i++] = up_enc_adrp_x16(at(0), slot_va);
    out[i++] = up_enc_add_x16(lo);        // x16 = slot
    out[i++] = up_enc_add_x17_imm(UP_SLOT_LOCK);  // x17 = slot + 24
    out[i++] = up_enc_ldxr_lock();
    out[i++] = up_enc_cbnz_w(9, 4, 18);   // ocupado -> direto
    out[i++] = UP_MOVZ_W9_1;
    out[i++] = up_enc_stxr_lock();
    out[i++] = up_enc_cbnz_w(10, 7, 3);   // perdeu corrida -> refaz ldxr
    out[i++] = up_enc_str_x(30, UP_SLOT_LR);
    out[i++] = UP_LDR_X16_ORIG;
    out[i++] = UP_BLR_X16;
    out[i++] = up_enc_adrp_x16(at(11), slot_va);
    out[i++] = up_enc_add_x16(lo);
    if (is_float) {
        out[i++] = up_enc_ldr_s1();
        out[i++] = UP_FMUL_S0;
    } else {
        out[i++] = up_enc_ldr_x(1, UP_SLOT_VALUE);
        out[i++] = UP_MUL_X0;
    }
    out[i++] = up_enc_str_x(31, UP_SLOT_LOCK);  // xzr destrava
    out[i++] = up_enc_ldr_x(30, UP_SLOT_LR);
    out[i++] = UP_RET;
    // caminho direto (18): sem lock, sem save — lr intacto, só repassa.
    // x16 ainda = slot (só adrp/add dele até aqui; cbnz não mexe).
    out[i++] = UP_LDR_X16_ORIG;
    out[i++] = UP_BLR_X16;
    out[i++] = UP_RET;
    return i;  // 21
}
#define UP_MUL_THUNK_WORDS 21
// ponytail: teto de 24 thunks mul (24*84 = 2016 bytes numa página RX de 4096).
// Passou disso, a regra vira log e o jogo segue sem ela.
#define UP_MUL_MAX 24
