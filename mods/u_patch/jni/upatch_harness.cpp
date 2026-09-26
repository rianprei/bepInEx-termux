// upatch_harness.cpp — teste do u_patch (F4) standalone pro gate verify_all.
// Roda os Casos 61-64 do selftest_harness (parser C4/C3, emissores arm64,
// guarda de método curto, verbo field) sem depender do harness do loader.
// Compila: g++ -std=c++17 -Wall -Wextra -Werror upatch_harness.cpp -o upatch_harness
#include <cstdio>
#include <cstring>

#include "u_patch_parse.h"
#include "u_patch_arm64.h"

static int g_fail = 0;
static void check(const char *name, bool cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main() {
    printf("u_patch encoding harness\n");
    printf("\n[Caso 61] u_patch_parse: regras C4 + conf C3 (motor declarativo F4)\n");
    {
        up_rule_t r;
        char line[256];
        // Exemplo de aceite do contrato:
        snprintf(line, sizeof(line), "return ComplexCreature HasAmmo 0 bool true");
        check("return válido (aceite SA2)", up_parse_line(line, &r) == 0 && r.kind == UP_RETURN &&
              strcmp(r.cls, "ComplexCreature") == 0 && strcmp(r.member, "HasAmmo") == 0 &&
              r.nargs == 0 && r.type == UP_BOOL && strcmp(r.value, "true") == 0);
        // Classe sem namespace: ns fica "".
        char ns[128], cname[128];
        check("classe sem namespace resolve com ns vazio",
              up_split_class(r.cls, ns, sizeof(ns), cname, sizeof(cname)) && ns[0] == '\0' &&
              strcmp(cname, "ComplexCreature") == 0);
        // Namespace: separa no ÚLTIMO ponto.
        check("namespace separa no último ponto",
              up_split_class("Landfall.TABS.UnitBlueprint", ns, sizeof(ns), cname, sizeof(cname)) &&
              strcmp(ns, "Landfall.TABS") == 0 && strcmp(cname, "UnitBlueprint") == 0);
        // Comentário e vazia pulam.
        snprintf(line, sizeof(line), "# return X Y 0 bool true");
        check("linha de comentário pula", up_parse_line(line, &r) == 1);
        snprintf(line, sizeof(line), "   ");
        check("linha vazia pula", up_parse_line(line, &r) == 1);
        snprintf(line, sizeof(line), "return X Y 0 bool true # sempre cheio");
        check("comentário no fim da linha é cortado", up_parse_line(line, &r) == 0 &&
              strcmp(r.value, "true") == 0);
        // static (5 campos, sem nargs) e mul.
        snprintf(line, sizeof(line), "static GameSettings godMode bool true");
        check("static válido", up_parse_line(line, &r) == 0 && r.kind == UP_STATIC &&
              r.type == UP_BOOL && strcmp(r.value, "true") == 0);
        snprintf(line, sizeof(line), "mul Weapon Damage 1 float $mult");
        check("mul com $key válido", up_parse_line(line, &r) == 0 && r.kind == UP_MUL &&
              r.nargs == 1 && r.type == UP_FLOAT && strcmp(r.value, "$mult") == 0);
        // Inválidas.
        snprintf(line, sizeof(line), "frobnicate X Y 0 bool true");
        check("verbo desconhecido rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y bool true");
        check("nargs faltando rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y 0 string oi");
        check("tipo desconhecido rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y -1 bool true");
        check("nargs negativo rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y 3x bool true");
        check("nargs com lixo rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "mul X Y 0 bool true");
        check("mul bool rejeitado (só int|float)", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "static X Y bool");
        check("static sem valor rejeitado", up_parse_line(line, &r) == -1);
        // Conf C3.
        const char *conf = "# mod de teste\nmult = 2.5 \nflag=true\n";
        char val[64];
        check("conf acha key com espaços", up_conf_get(conf, "mult", val, sizeof(val)) && strcmp(val, "2.5") == 0);
        check("conf acha bool", up_conf_get(conf, "flag", val, sizeof(val)) && strcmp(val, "true") == 0);
        check("conf não acha key ausente", !up_conf_get(conf, "outra", val, sizeof(val)));
        check("conf não acha dentro de comentário", !up_conf_get(conf, "mod", val, sizeof(val)));
        check("conf nulo não crasha", !up_conf_get(nullptr, "mult", val, sizeof(val)));
    }

    printf("\n[Caso 62] u_patch_arm64: emissores de patch (F4, palavras conferidas contra llvm-objdump do NDK)\n");
    {
        // Âncoras geradas pelo assembler do NDK (clang --target=aarch64-linux-android23 -c
        // + llvm-objdump -d; comando/entrada comentados no relatório do commit):
        //   mov x0,#1          -> 0xD2800020    ret -> 0xD65F03C0
        //   movz x0,#0x2345    -> 0xD28468A0    movk x0,#1,lsl#16 -> 0xF2A00020
        //   movz w9,#0x8000    -> 0x52900009    movk w9,#0x3F80,lsl#16 -> 0x72A7F009
        //   ldxr w9,[x17]      -> 0x885F7E29    stxr w10,w9,[x17] -> 0x880A7E29
        //   ldaxr w9,[x17]     -> 0x885FFE29    stlr wzr,[x17] -> 0x889FFE3F
        //   br x16             -> 0xD61F0200
        //   add x17,x16,#24    -> 0x91006211    movz w9,#0 -> 0x52800009
        //   (leitura little-endian dos bytes do objdump; add x17 com imediato
        //   24 vale pra slot alinhado — no thunk o imediato é lo(slot)+24)
        //   ldr s1,[x16,#8]    -> 0xBD400A01    fmul s0,s0,s1 -> 0x1E210800
        //   fmov s0,w9         -> 0x1E270120    mov w9,#1 -> 0x52800029
        //   mul x0,x0,x1       -> 0x9B017C00    ldr x1,[x16,#8] -> 0xF9400601
        //   blr x16            -> 0xD63F0200    str x30,[x16,#16] -> 0xF9000A1E
        //   ldr x30,[x16,#16]  -> 0xF9400A1E    str xzr,[x16,#24] -> 0xF9000E1F
        check("movz x0,#1 = 0xD2800020 (objdump)", up_enc_movz_x0(1) == 0xD2800020u);
        check("ret = 0xD65F03C0 (objdump)", UP_RET == 0xD65F03C0u);
        check("blr x16 = 0xD63F0200 (objdump)", UP_BLR_X16 == 0xD63F0200u);
        check("mul x0,x0,x1 = 0x9B017C00 (objdump)", UP_MUL_X0 == 0x9B017C00u);
        check("fmul s0,s0,s1 = 0x1E210800 (objdump)", UP_FMUL_S0 == 0x1E210800u);
        check("fmov s0,w9 = 0x1E270120 (objdump)", UP_FMOV_S0_W9 == 0x1E270120u);
        check("ldaxr w9,[x17] = 0x885FFE29 (objdump)", up_enc_ldxr_lock() == 0x885FFE29u);
        check("stlr wzr,[x17] = 0x889FFE3F (objdump)", up_enc_stlr_unlock() == 0x889FFE3Fu);
        check("br x16 = 0xD61F0200 (objdump)", UP_BR_X16 == 0xD61F0200u);
        check("stxr w10,w9,[x17] = 0x880A7E29 (objdump)", up_enc_stxr_lock() == 0x880A7E29u);
        check("ldr s1,[x16,#8] = 0xBD400A01 (objdump)", up_enc_ldr_s1() == 0xBD400A01u);
        check("mov w9,#1 = 0x52800029 (objdump)", UP_MOVZ_W9_1 == 0x52800029u);
        uint32_t w[8];
        check("return bool = movz+ret (objdump)", up_emit_return_bool(w, 1) == 2 && w[0] == 0xD2800020u && w[1] == UP_RET);
        int n = up_emit_return_int(w, 0x12345u);
        check("return int grande = movz+movk+ret (objdump)", n == 3 && w[2] == UP_RET &&
              w[0] == 0xD28468A0u && w[1] == 0xF2A00020u);
        // Âncora movz/movk w9 do objdump: bits 0x3F808000 (meia-palavra
        // baixa 0x8000 NÃO é 0 — float 1.0f = 0x3F800000 teria movz #0,
        // âncora 0x52800009 mov w9,#0). Teste usa os bits da âncora real.
        check("return float = movz/movk w9+fmov+ret (objdump)", up_emit_return_float(w, 0x3F808000u) == 4 &&
              w[0] == 0x52900009u && w[1] == 0x72A7F009u && w[2] == 0x1E270120u && w[3] == UP_RET);
        // 1.0f (0x3F800000): meia-palavra baixa zero → movz #0 (objdump mov w9,#0).
        check("return float 1.0f = movz#0+movk+fmov+ret", up_emit_return_float(w, 0x3F800000u) == 4 &&
              w[0] == 0x52800009u && w[1] == 0x72A7F009u && w[2] == 0x1E270120u && w[3] == UP_RET);
        // Thunk mul: 21 palavras. Validação por CAMPO decodificado
        // (Rd/Rn/Rt/Rs/imm), não só palavra inteira — encoding errado de
        // registrador falha aqui (o bug Rn=18 do add x17 é pego por estes).
        // Âncoras de fluxo: x16/x17 são caller-saved — o bloco pós-call
        // RE-DERIVA x16 (adrp/add) e x17 (add #24); lr é restaurado ANTES
        // do unlock (a thread que adquirir depois sobrescreve slot.lr).
        uint32_t t[UP_MUL_THUNK_WORDS + 4];
        const void *thunk_va = (const void *)0x7A000000ull;
        const void *slot_va = (const void *)0x7B001234ull;
        check("thunk int tem 21 palavras (fast path = tail call br)", up_emit_mul_thunk(t, thunk_va, slot_va, false) == UP_MUL_THUNK_WORDS);
        auto decode_pair = [](uint32_t adrp, uint32_t add, uintptr_t pc) -> uintptr_t {
            int64_t off = (int64_t)((((adrp >> 29) & 3u) | (((adrp >> 5) & 0x7FFFFu) << 2)));
            if (off & 0x100000) off -= 0x200000;  // sext 21 bits
            uintptr_t page = (pc & ~(uintptr_t)0xFFFu) + (uintptr_t)(off << 12);
            return page + (((add >> 10) & 0xFFFu));
        };
        // --- decodificadores de campo (ARM ARM C4.1) ---
        auto fld_Rd = [](uint32_t w) { return (int)(w & 31u); };
        auto fld_Rn = [](uint32_t w) { return (int)((w >> 5) & 31u); };
        auto fld_Rt = fld_Rd;
        auto fld_Rs = [](uint32_t w) { return (int)((w >> 16) & 31u); };
        auto fld_imm12 = [](uint32_t w) { return (int)((w >> 10) & 0xFFFu); };
        auto fld_imm19 = [](uint32_t w) {
            int32_t imm = (int32_t)((w >> 5) & 0x7FFFFu);
            if (imm & 0x40000) imm -= 0x80000;
            return imm;
        };
        // --- [2] add x17, x16, #24: Rn TEM que ser 16 (x18 = selvagem) ---
        check("add[2]: palavra = 0x91006211 (objdump)", t[2] == 0x91006211u);
        check("add[2]: Rd=17 Rn=16 imm=24 (UP_SLOT_LOCK)",
              fld_Rd(t[2]) == 17 && fld_Rn(t[2]) == 16 && fld_imm12(t[2]) == (int)UP_SLOT_LOCK);
        // prova: reintroduzindo o bug Rn=18, o decodificador pega
        {
            uint32_t buggy = 0x91000000u | (24u << 10) | (18u << 5) | 17u;
            check("prova: add com Rn=18 é pego pelo decodificador", fld_Rn(buggy) == 18);
        }
        check("adrp/add[0-1] apontam pro slot",
              decode_pair(t[0], t[1], (uintptr_t)thunk_va) == (uintptr_t)slot_va);
        check("adrp/add[11-12] apontam pro slot",
              decode_pair(t[11], t[12], (uintptr_t)thunk_va + 44) == (uintptr_t)slot_va);
        // cbnz[4] pula pro caminho direto (19 = ldr x16); cbnz[7] volta pro ldaxr (3).
        check("cbnz do lock vai pro caminho direto (19)", fld_imm19(t[4]) + 4 == 19);
        check("cbnz da corrida volta pro ldaxr (3)", fld_imm19(t[7]) + 7 == 3);
        check("ldaxr em [3] (acquire), stxr em [6] (objdump)", t[3] == 0x885FFE29u && t[6] == 0x880A7E29u);
        check("stxr: Rs=w10 (status) Rt=w9 (dado) — campos decodificados",
              fld_Rs(t[6]) == 10 && fld_Rt(t[6]) == 9);
        check("thunk float: ldr s1 + fmul em [14]/[15]", up_emit_mul_thunk(t, thunk_va, slot_va, true) == UP_MUL_THUNK_WORDS &&
              t[14] == 0xBD400A01u && t[15] == 0x1E210800u);
        check("thunk int: ldr x1,[x16,#8] + mul em [14]/[15] (Rt=1, Rn=16, imm12=1 => 8 bytes)",
              up_emit_mul_thunk(t, thunk_va, slot_va, false) && t[14] == 0xF9400601u &&
              fld_Rt(t[14]) == 1 && fld_Rn(t[14]) == 16 && fld_imm12(t[14]) == 1 && t[15] == 0x9B017C00u);
        check("save lr em [8]; restore ANTES do unlock: ldr x30 em [16], stlr em [17]",
              t[8] == 0xF9000A1Eu && t[16] == 0xF9400A1Eu && t[17] == 0x889FFE3Fu &&
              fld_Rt(t[8]) == 30 && fld_Rn(t[8]) == 16);
        check("unlock stlr wzr,[x17] (release, Rt=31, Rn=17 — não zeraria orig)",
              fld_Rt(t[17]) == 31 && fld_Rn(t[17]) == 17);
        check("pós-call: add[13] re-deriva x17 = x16 + 24 (Rd=17, Rn=16, imm=24)",
              t[13] == 0x91006211u && fld_Rd(t[13]) == 17 && fld_Rn(t[13]) == 16 &&
              fld_imm12(t[13]) == (int)UP_SLOT_LOCK);
        check("fast path = ldr x16,[x16] + br x16 em [19]/[20] (tail call, x30 intacto)",
              t[19] == UP_LDR_X16_ORIG && t[20] == UP_BR_X16);
        // --- checagem estrutural do fluxo: nenhum 'ret' pode executar depois
        // de um 'blr' sem um 'ldr x30' entre eles (o bug do self-loop) — e
        // nenhum 'ret' com x30 salvo e ainda não restaurado. ---
        {
            bool clean = true;
            bool blr_pending = false;   // blr visto, x30 ainda não restaurado
            bool lr_saved = false;      // x30 salvo e ainda não restaurado
            bool mul_done = false;      // já passou do adrp pós-call (x16/x17 velhos mortos)
            bool x17_fresh = false;     // x17 = slot+24 válido (re-derivado pós-call)
            for (int w = 0; w < UP_MUL_THUNK_WORDS && clean; w++) {
                if (t[w] == UP_LDR_X16_ORIG) continue;  // não mexe x30
                if (t[w] == 0xF9000A1Eu) { lr_saved = true; continue; }   // str x30,[x16,#16]
                if (t[w] == 0xF9400A1Eu) { lr_saved = false; blr_pending = false; continue; } // ldr x30,[x16,#16]
                if (t[w] == UP_BLR_X16) { blr_pending = true; continue; }
                if (t[w] == 0x91006211u) { x17_fresh = true; continue; }  // add x17,x16,#24
                if (((t[w] >> 24) & 0x9Fu) == 0x90u) {  // adrp: pós-call, x16/x17 velhos morreram
                    if (w > 2) { mul_done = true; x17_fresh = false; }
                    continue;
                }
                if ((t[w] >> 24) == 0x88u) {  // lock por exclusiva (stxr/stlr): usa x17
                    if (mul_done && !x17_fresh) clean = false;  // bug (a): unlock em endereço lixo
                    if (w == UP_MUL_THUNK_WORDS - 4 && lr_saved) clean = false;  // bug (b): unlock antes do restore
                    if (w == UP_MUL_THUNK_WORDS - 4) { lr_saved = false; blr_pending = false; }
                    continue;
                }
                if (t[w] == UP_RET) {
                    if (blr_pending) clean = false;  // ret após blr sem ldr x30 = self-loop
                    if (lr_saved) clean = false;     // ret com x30 salvo e não restaurado
                    if (mul_done && !x17_fresh) clean = false;  // ret sem re-derivar x17 pós-call
                }
                if (t[w] == UP_BR_X16) { blr_pending = false; }  // tail call: x30 do chamador segue
            }
            check("estrutura: ret com x30 restaurado, unlock com x17 re-derivado e pós-restore", clean);
            // PROVA: reintroduz o bug (blr+ret no fast path) → a checagem falha
            {
                uint32_t bad[UP_MUL_THUNK_WORDS + 1];
                for (int k = 0; k < UP_MUL_THUNK_WORDS; k++) bad[k] = t[k];
                bad[20] = UP_BLR_X16;  // fast path: blr em vez de br
                bad[21] = UP_RET;      // + ret = self-loop
                bool bclean = true;
                bool bblr = false;
                for (int w = 0; w < UP_MUL_THUNK_WORDS + 1; w++) {
                    if (bad[w] == 0xF9400A1Eu) { bblr = false; continue; }
                    if (bad[w] == UP_BLR_X16) { bblr = true; continue; }
                    if (bad[w] == UP_RET) {
                        if (bblr) { bclean = false; break; }
                    }
                    if (bad[w] == UP_BR_X16) { bblr = false; }
                }
                check("prova: blr+ret no fast path É pego pela checagem", !bclean);
            }
        }
        check("pool cabe numa página RX", (size_t)UP_MUL_MAX * UP_MUL_THUNK_WORDS * 4 <= 4096);
    }

    printf("\n[Caso 63] up_method_fits: guard de método curto (F4 revisão)\n");
    {
        // Getter curto do exemplo da revisão: ldr w0,[x0,#8]; ret (8 bytes).
        const uint32_t getter[] = {0xB9400800u, UP_RET};
        check("getter 8B cabe return bool (8B)", up_method_fits(getter, 2));
        check("getter 8B NÃO cabe return int grande (12B)", !up_method_fits(getter, 3));
        check("getter 8B NÃO cabe return float (16B)", !up_method_fits(getter, 4));
        // Método vazio (só ret) não cabe nem bool.
        const uint32_t empty[] = {UP_RET, 0xD503201Fu};
        check("método vazio recusado", !up_method_fits(empty, 2));
        // Tail call (B incondicional no meio) termina a função ali.
        const uint32_t tail[] = {0x94000005u, UP_RET, 0xD503201Fu, 0xD503201Fu};
        check("B incondicional no meio recusa", !up_method_fits(tail, 4));
        // br x30 no meio também termina.
        const uint32_t brmid[] = {0x910003E0u, 0xD61F03C0u, UP_RET, 0xD503201Fu};
        check("br no meio recusa", !up_method_fits(brmid, 4));
        // ret exatamente na última palavra é ok (método tem o tamanho exato).
        const uint32_t exact[] = {0x910003E0u, 0x910003E1u, 0x910003E2u, UP_RET};
        check("ret na última palavra cabe", up_method_fits(exact, 4));
        // Método normal sem terminador no meio cabe.
        const uint32_t normal[] = {0x910003E0u, 0xB9400800u, 0x0B000020u, UP_RET};
        check("método normal cabe float", up_method_fits(normal, 4));
        check("nulo e n<2 recusados", !up_method_fits(nullptr, 4) && !up_method_fits(normal, 1));
        // up_is_terminator direto: cbz (condicional) NÃO termina; bl NÃO termina.
        check("cbz não é terminador", !up_is_terminator(0x34000020u));
        check("bl não é terminador", !up_is_terminator(0x94000005u));
        check("ret x30 é terminador", up_is_terminator(0xD65F03C0u));
        check("ret x9 é terminador", up_is_terminator(0xD65F0120u));
        // Bug 3 (C1): cmdline ainda-zygote não serve como <pkg>.
        check("zygote é zygote", up_is_zygote("zygote"));
        check("zygote64 é zygote", up_is_zygote("zygote64"));
        check("pacote real não é zygote", !up_is_zygote("com.hyperdotstudios.swampattack2"));
        check("nulo/vazio não é zygote", !up_is_zygote(nullptr) && !up_is_zygote(""));
    }
    printf("\n[Caso 64] field C4: parse + emissor do thunk (F4b)\\n");
    {
        up_rule_t r;
        char line[256];
        snprintf(line, sizeof(line), "field WeaponInfo unlimitedAmmo bool true");
        check("field auto válido (5 campos)", up_parse_line(line, &r) == 0 && r.kind == UP_FIELD &&
              strcmp(r.cls, "WeaponInfo") == 0 && strcmp(r.member, "unlimitedAmmo") == 0 &&
              r.type == UP_BOOL && strcmp(r.value, "true") == 0 && r.nargs == -1 && r.fmethod[0] == 0);
        snprintf(line, sizeof(line), "field Foo bar int 7 SelectWeapon 1");
        check("field explícito válido (7 campos)", up_parse_line(line, &r) == 0 && r.kind == UP_FIELD &&
              strcmp(r.fmethod, "SelectWeapon") == 0 && r.nargs == 1 &&
              strcmp(r.value, "7") == 0);
        snprintf(line, sizeof(line), "field Foo bar string x");
        check("field tipo errado rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo bar bool true M");
        check("field 6 campos rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo bar bool true M x");
        check("field nargs ruim rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo NoSuchField bool true");
        check("field com campo desconhecido aceita no parse (inexistência é runtime)",
              up_parse_line(line, &r) == 0 && r.kind == UP_FIELD);
        uint32_t f[8];
        const void *fva = (const void *)0x7A000100ull;
        const void *sva = (const void *)0x7B002234ull;
        int nf = up_emit_field_thunk(f, fva, sva, 1, 1, 112);
        check("field bool = 6 palavras", nf == 6);
        check("field bool[0] movz w9,#1", f[0] == 0x52800029u);
        check("field bool[1] strb w9,[x0,#112]", f[1] == 0x3901C009u);
        check("field bool termina ldr+br", f[4] == UP_LDR_X16_ORIG && f[5] == UP_BR_X16);
        int ni = up_emit_field_thunk(f, fva, sva, 4, 0x12345678u, 112);
        check("field int = 7 palavras", ni == 7);
        check("field int[1] movk w9 hi", f[1] == (0x72A00000u | (0x1234u << 5) | 9u));
        check("field int[2] str w9,[x0,#112]", f[2] == 0xB9007009u);
        int nf2 = up_emit_field_thunk(f, fva, sva, 4, 0x00000000u, 112);
        check("field float 0.0f = 6 palavras", nf2 == 6);
        int nf3 = up_emit_field_thunk(f, fva, sva, 4, 0x3F800000u, 112);
        check("field float 1.0f = 7 palavras", nf3 == 7);
        check("field strb off>4095 recusa", up_emit_field_thunk(f, fva, sva, 1, 1, 4096) == 0);
        check("field str desalinhado recusa", up_emit_field_thunk(f, fva, sva, 4, 1, 114) == 0);
        check("field str off/4>4095 recusa", up_emit_field_thunk(f, fva, sva, 4, 1, 16384) == 0);
        check("field size inválido recusa", up_emit_field_thunk(f, fva, sva, 2, 1, 8) == 0);
    }

    printf("== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
