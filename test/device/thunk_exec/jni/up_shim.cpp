// Shim C++ mínimo: o emissor (u_patch_arm64.h) é header-only C++ (usa
// lambdas/auto); o executor é C. Isso só encaminha as chamadas.
#include "u_patch_arm64.h"

extern "C" long up_emit_mul_thunk_c(uint32_t *out, const void *thunk_va,
                                    const void *slot_va, int is_float) {
    return up_emit_mul_thunk(out, thunk_va, slot_va, is_float != 0);
}

extern "C" int up_mul_thunk_words_c(void) {
    return UP_MUL_THUNK_WORDS;
}

extern "C" int up_emit_field_thunk_c(uint32_t *out, const void *thunk_va,
                                     const void *slot_va, int size, uint32_t bits,
                                     uint32_t off) {
    return up_emit_field_thunk(out, thunk_va, slot_va, size, bits, off);
}

extern "C" int up_field_thunk_words_max_c(void) {
    return UP_FIELD_THUNK_WORDS_MAX;
}
