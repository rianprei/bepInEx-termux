// fuzz_upatch_encoder — alvo de fuzzing do ENCODER DE THUNK e dos limites
// de valor/nargs do u_patch (mods/u_patch/jni/u_patch_arm64.h, header-only,
// sem plataforma).
//
// Cobre as emissores de patch arm64 que rodam DENTRO do processo do jogo:
//   1. up_emit_return_bool/int/float — Sequências de retorno (movz/movk+ret)
//   2. up_emit_field_thunk — Thunk de campo (cbz this-null, str/ldr/br)
//   3. up_enc_adrp_x16/add_x16 — Cálculo de endereço relativo a PC
//   4. up_enc_strb_w9/str_w9 — Limites de offset (strb 0..4095, str %4==0)
//   5. up_enc_movz/movk — Imediatos de 16 bits
//
// O que NÃO dá para fuzzer sem IL2CPP real:
//   - up_resolve_field_type (depende de Il2CppClass/FieldInfo do jogo)
//   - up_value_type_check (depende de Il2CppType real)
//   - up_patch_code (depende de mprotect/Dobby no processo do jogo)
//   - up_apply_return/mul/static (orquestra tudo acima + runtime IL2CPP)
//
// Compilar:
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//           -I../../mods/u_patch/jni fuzz_upatch_encoder.cpp -o fuzz_upatch_encoder

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../mods/u_patch/jni/u_patch_arm64.h"

namespace {

constexpr size_t kMaxInput = 256;

// Buffer de saída dos emissores (máximo: thunk field com movk = 10 palavras)
uint32_t g_out[16];

struct Derived {
    uint16_t imm;      // imediato para movz/movk
    uint32_t value;    // valor de 32 bits para return int/float
    uint32_t bits;     // bits para field thunk
    uint32_t off;      // offset para str/strb
    int size;          // 1 (bool) ou 4 (int/float)
    uint64_t pc;       // PC para adrp
    uint64_t target;   // target para adrp
};

Derived derive(const uint8_t *data, size_t size) {
    Derived d = {};
    if (size >= 2) d.imm = (uint16_t)(data[0] | (data[1] << 8));
    if (size >= 6) d.value = (uint32_t)(data[2] | (data[3] << 8) | (data[4] << 16) | ((uint32_t)data[5] << 24));
    if (size >= 10) d.bits = (uint32_t)(data[6] | (data[7] << 8) | (data[8] << 16) | ((uint32_t)data[9] << 24));
    if (size >= 14) d.off = (uint32_t)(data[10] | (data[11] << 8) | (data[12] << 16) | ((uint32_t)data[13] << 24));
    if (size >= 15) d.size = (data[14] % 2 == 0) ? 1 : 4;
    if (size >= 16) {
        // PC e target: usa bytes do input com folha para não overflow
        d.pc = (uint64_t)data[0] * 4096;
        d.target = (uint64_t)data[size - 1] * 4096;
    }
    return d;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > kMaxInput) size = kMaxInput;

    Derived d = derive(data, size);

    // 1) up_emit_return_bool: sempre 2 palavras (movz + ret)
    {
        uint32_t out[4];
        int n = up_emit_return_bool(out, (int)d.value);
        if (n != 2) __builtin_trap();
        if (out[1] != UP_RET) __builtin_trap();
    }

    // 2) up_emit_return_int: 2 ou 3 palavras (movz + [movk] + ret)
    {
        uint32_t out[4];
        int n = up_emit_return_int(out, d.value);
        if (n < 2 || n > 3) __builtin_trap();
        if (out[n - 1] != UP_RET) __builtin_trap();
        // Verifica que o valor codificado bate com o original
        uint32_t lo = out[0] & 0xFFFFu;
        uint32_t hi = (n == 3) ? ((out[1] >> 5) & 0xFFFFu) : 0;
        uint32_t reconstructed = lo | (hi << 16);
        if (reconstructed != (d.value & 0xFFFFFFFFu)) __builtin_trap();
    }

    // 3) up_emit_return_float: sempre 4 palavras
    {
        uint32_t out[8];
        int n = up_emit_return_float(out, d.bits);
        if (n != 4) __builtin_trap();
        if (out[2] != UP_FMOV_S0_W9) __builtin_trap();
        if (out[3] != UP_RET) __builtin_trap();
        // Reconstrói os bits do float
        uint32_t lo = (out[0] >> 5) & 0xFFFFu;
        uint32_t hi = (out[1] >> 5) & 0xFFFFu;
        uint32_t reconstructed = lo | (hi << 16);
        if (reconstructed != d.bits) __builtin_trap();
    }

    // 4) up_emit_field_thunk: 9 ou 10 palavras; 0 = offset inválido
    {
        uint32_t out[16];
        int n = up_emit_field_thunk(out, (const void *)(uintptr_t)d.pc,
                                   (const void *)(uintptr_t)d.target,
                                   d.size, d.bits, d.off);
        // size 1 (bool): off 0..4095; size 4 (int): off %4==0 e off/4 <= 4095
        if (d.size == 1 && d.off > 4095) {
            if (n != 0) __builtin_trap();
        } else if (d.size == 4 && (d.off % 4 != 0 || d.off / 4 > 4095)) {
            if (n != 0) __builtin_trap();
        } else {
            if (n != 9 && n != 10) __builtin_trap();
            // Verifica cbz na palavra 2 (this-null guard)
            if ((out[2] & 0xFF00001Fu) != 0xB4000000u) __builtin_trap();
            // Verifica ldr x16,[x16] e br x16 no final
            if (out[n - 2] != UP_LDR_X16_ORIG) __builtin_trap();
            if (out[n - 1] != UP_BR_X16) __builtin_trap();
        }
    }

    // 5) up_enc_adrp_x16 + up_enc_add_x16: verifica reconstrução de endereço
    {
        uint32_t adrp = up_enc_adrp_x16((const void *)(uintptr_t)d.pc,
                                        (const void *)(uintptr_t)d.target);
        uint32_t add = up_enc_add_x16((uint32_t)(d.target & 0xFFFu));
        // Reconstrói: página do target relativa ao PC + lo12
        int64_t off = ((int64_t)(uintptr_t)d.target >> 12) - ((int64_t)(uintptr_t)d.pc >> 12);
        uint32_t expected_adrp = 0x90000010u | (((uint32_t)(off & 3u)) << 29) | ((((uint32_t)off >> 2) & 0x7FFFFu) << 5);
        if (adrp != expected_adrp) __builtin_trap();
        if (add != (0x91000210u | (((uint32_t)(d.target & 0xFFFu) & 0xFFFu) << 10))) __builtin_trap();
    }

    // 6) up_enc_strb_w9/str_w9: limites de offset
    {
        // strb: off 0..4095
        uint32_t strb = up_enc_strb_w9((uint16_t)(d.off & 0xFFF));
        if (strb != (0x39000000u | (((uint32_t)(d.off & 0xFFF)) << 10) | 9u)) __builtin_trap();

        // str: off múltiplo de 4, 0..16380
        uint32_t str = up_enc_str_w9(d.off);
        uint32_t expected_str = 0xB9000000u | (((d.off >> 2) & 0xFFFu) << 10) | 9u;
        if (str != expected_str) __builtin_trap();
    }

    // 7) up_enc_movz/movk: imediato de 16 bits
    {
        if (up_enc_movz_x0(d.imm) != (0xD2800000u | ((uint32_t)d.imm << 5))) __builtin_trap();
        if (up_enc_movk_x0_16(d.imm) != (0xF2A00000u | ((uint32_t)d.imm << 5))) __builtin_trap();
        if (up_enc_movz_w9(d.imm) != (0x52800000u | ((uint32_t)d.imm << 5) | 9u)) __builtin_trap();
        if (up_enc_movk_w9_16(d.imm) != (0x72A00000u | ((uint32_t)d.imm << 5) | 9u)) __builtin_trap();
    }

    return 0;
}
