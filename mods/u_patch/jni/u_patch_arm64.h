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

// Opcodes fixos (ARM ARM, testados no harness contra os valores conhecidos).
#define UP_RET 0xD65F03C0          // ret
#define UP_BLR_X16 0xD63F0200      // blr x16
#define UP_LDR_X16_ORIG 0xF9400210  // ldr x16, [x16, #0]
#define UP_MUL_X0 0x9B017C00        // mul x0, x0, x1
#define UP_FMUL_S0 0x1E211C00       // fmul s0, s0, s1
#define UP_FMOV_S0_W9 0x1E200120    // fmov s0, w9
#define UP_MOVZ_W17_1 0x52800031    // mov w17, #1

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
// ldxr w17, [x16, #24]
static inline uint32_t up_enc_ldxr_lock() {
    return 0x885F7C00u | (16u << 5) | 17u;
}
// stxr w18, w17, [x16, #24]
static inline uint32_t up_enc_stxr_lock() {
    return 0x88007C00u | (17u << 16) | (16u << 5) | 18u;
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
    return 0x3D400000u | ((2u & 0xFFFu) << 10) | (16u << 5) | 1u;
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

// Thunk mul: chama orig com regs intactos, multiplica o retorno.
// R0-R7/stack preservados (só x16/x17/x18/x1 mexidos pós-chamada, todos
// caller-saved). Trava curta: reentrância cai no caminho direto (sem mul,
// sem hang). Retorna nº de palavras (20 = 80 bytes).
// Layout: 0-1 adrp/add slot, 2-6 lock, 7 str lr, 8 ldr orig, 9 blr,
// 10-11 adrp/add, 12 ldr fator, 13 mul, 14 unlock, 15 restore lr, 16 ret,
// 17-19 caminho direto (ldr orig, blr, ret).
static inline int up_emit_mul_thunk(uint32_t *out, const void *thunk_va, const void *slot_va, bool is_float) {
    uintptr_t slot = (uintptr_t)slot_va;
    uint32_t lo = (uint32_t)(slot & 0xFFFu);
    const uint32_t *base = out;
    auto at = [&](int i) -> const void * { return (const uint8_t *)thunk_va + (size_t)i * 4; };
    int i = 0;
    out[i++] = up_enc_adrp_x16(at(0), slot_va);
    out[i++] = up_enc_add_x16(lo);
    out[i++] = up_enc_ldxr_lock();
    out[i++] = up_enc_cbnz_w(17, 3, 17);  // ocupado -> direto
    out[i++] = UP_MOVZ_W17_1;
    out[i++] = up_enc_stxr_lock();
    out[i++] = up_enc_cbnz_w(18, 6, 2);  // perdeu corrida -> tenta de novo
    out[i++] = up_enc_str_x(30, UP_SLOT_LR);
    out[i++] = UP_LDR_X16_ORIG;
    out[i++] = UP_BLR_X16;
    out[i++] = up_enc_adrp_x16(at(10), slot_va);
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
    // caminho direto (17): sem lock, sem save — lr intacto, só repassa.
    out[i++] = UP_LDR_X16_ORIG;  // x16 ainda = slot (cbnz não mexe)
    out[i++] = UP_BLR_X16;
    out[i++] = UP_RET;
    (void)base;
    return i;  // 20
}
#define UP_MUL_THUNK_WORDS 20
// ponytail: teto de 24 thunks mul (24*80 = 1920 bytes numa página RX de
// 4096). Passou disso, a regra vira log e o jogo segue sem ela.
#define UP_MUL_MAX 24
