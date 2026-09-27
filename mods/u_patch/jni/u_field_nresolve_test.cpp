// u_field_nresolve_test.cpp — teste HOST da cadeia de resolução de tipo do u_patch,
// com um Il2Cpp FALSO que devolve nullptr em CADA elo.
//
// O que o teste prova, e por que cada caso existe:
//
//   ACHADO REAL (device POCO C75, SA2): a regra
//   `field WeaponInfo unlimitedAmmo bool true` derrubou o jogo com SIGSEGV em
//   il2cpp_type_get_name (x0 = 0, fault addr 0x135). Simbolizado contra o
//   build da base 5ed8019:
//
//     #04 u_patch.so+0x1bb34 → up_field_type_name  u_patch_mod.cpp:375
//     #05 u_patch.so+0x1a5f0 → up_apply_field       u_patch_mod.cpp:523
//
//   A linha 375 passava a Il2CppClass* (resultado de class_from_type) para
//   il2cpp_type_get_name, que quer Il2CppType*. Type confusion. Aqui cada elo
//   da cadeia é testado com o stub devolvendo nullptr, e o caso OBRIGA que o
//   il2cpp_type_get_name receba o TYPE — o stub grava o ponteiro que recebeu
//   e o caso compara. É assim que a reintrodução do bug quebra o teste, e não
//   um "não deu crash" que passaria igual.
//
//   Um caso por elo, e cada um tem que dizer POR QUE recusou: um nullptr que
//   vira "recusou sem motivo" é indistinguível de um que vira crash no device.
//
// Roda no gate: o passo "u_patch encoding harness" do tools/verify_all.sh faz
// glob em mods/u_patch/**/*test*.cpp, entao este arquivo entra sozinho.
// Compilar na mao:
//   g++ -std=c++17 -Wall -Wextra -Werror -I jni mods/u_patch/jni/u_field_nresolve_test.cpp
//       -o u_field_nresolve

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "u_patch_resolve.h"
#include "u_patch_parse.h"

namespace {

int g_fail = 0;
void check(const char *name, bool cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

std::string case_label(unsigned local_id) {
    // The gate injects a global range; standalone output stays file-qualified.
    const char *base_text = std::getenv("UP_FIELD_CASE_ID_BASE");
    if (base_text != nullptr && *base_text != '\0' && local_id > 0) {
        errno = 0;
        char *end = nullptr;
        const unsigned long base = std::strtoul(base_text, &end, 10);
        const unsigned long offset = static_cast<unsigned long>(local_id - 1);
        if (errno == 0 && end != base_text && *end == '\0' &&
            base <= ULONG_MAX - offset) {
            return "Caso " + std::to_string(base + offset);
        }
    }
    return "Caso u_field_nresolve/" + std::to_string(local_id);
}

void print_case(unsigned local_id, const char *description) {
    const std::string label = case_label(local_id);
    printf("%s[%s] %s\n", local_id == 1 ? "" : "\n", label.c_str(), description);
}

// --- o mundo que o fake representa -----------------------------------------
struct FakeType { char blob[32]; };
struct FakeClass { char blob[32]; };
struct FakeField { char blob[32]; };

FakeType g_type;
FakeClass g_class;
FakeField g_field;
const char *g_type_name = "System.Boolean";

// Qual elo devolve nullptr. Tudo que não for listado devolve o objeto válido.
enum NullAt {
    NUL_NONE = 0,
    NUL_API_FIELD_GET_TYPE,   // il2cpp_field_get_type não resolvido no boot
    NUL_API_CLASS_FROM_TYPE,  // il2cpp_class_from_type não resolvido
    NUL_API_TYPE_GET_NAME,    // il2cpp_type_get_name não resolvido
    NUL_FIELD,                // class_get_field_from_name devolveu null
    NUL_TYPE,                 // field_get_type devolveu null
    NUL_CLASS,                // class_from_type devolveu null
    NUL_NAME,                 // type_get_name devolveu null
    NUL_API_FREE,             // free ausente: nome do runtime não pode ser solto
} g_null_at = NUL_NONE;

// O ponteiro que o fake type_get_name RECEBEU. É a prova do type confusion:
// o correto é o Il2CppType* (&g_type), nunca o Il2CppClass* (&g_class).
const void *g_type_get_name_arg = nullptr;
int g_type_get_name_calls = 0;
int g_frees = 0;
std::string g_freed_values;

// "API ausente" (NUL_API_*) e "return null" (NUL_TYPE/NUL_CLASS/NUL_NAME)
// são defeitos DIFERENTES e o teste tem que cobrir os dois: a API ausente é o
// il2cpp_boot que não resolveu o símbolo (o ponteiro de função fica null e o
// chamador precisa recusar ANTES de chamar); o return null é a API presente
// e o runtime devolvendo nada. Por isso NUL_API_ é aplicado no make_il() e
// NUL_* dentro dos stubs.
const void *f_field_get_type(void *) {
    if (g_null_at == NUL_TYPE) return nullptr;
    return &g_type;
}
void *f_class_from_type(const void *) {
    if (g_null_at == NUL_CLASS) return nullptr;
    return &g_class;
}
char *f_type_get_name(const void *type) {
    g_type_get_name_calls++;
    g_type_get_name_arg = type;
    if (g_null_at == NUL_NAME) return nullptr;
    // o nome "pertence" ao type devolvido: se o chamador passar a classe, o
    // nome devolvido é de outra coisa (no runtime real, memory lida do
    // lugar errado). Copiamos de uma tabela para não devolver ponteiro
    // descartável.
    static char buf[256];
    snprintf(buf, sizeof(buf), "%s", g_type_name);
    return buf;
}
void f_free(void *p) {
    g_frees++;
    g_freed_values += p ? reinterpret_cast<const char *>(p) : "(null)";
}
bool f_class_is_valuetype(void *) { return false; }

Il2Cpp make_il() {
    Il2Cpp il = {};
    il.field_get_type = f_field_get_type;
    il.class_from_type = f_class_from_type;
    il.type_get_name = f_type_get_name;
    il.class_is_valuetype = f_class_is_valuetype;
    il.free = f_free;
    // il2cpp_boot deixa o ponteiro NULL quando o símbolo não existe. É o que
    // o chamador tem que checar antes de chamar.
    if (g_null_at == NUL_API_FIELD_GET_TYPE) il.field_get_type = nullptr;
    if (g_null_at == NUL_API_CLASS_FROM_TYPE) il.class_from_type = nullptr;
    if (g_null_at == NUL_API_TYPE_GET_NAME) il.type_get_name = nullptr;
    return il;
}

void reset() {
    g_null_at = NUL_NONE;
    g_type_get_name_arg = nullptr;
    g_type_get_name_calls = 0;
    g_frees = 0;
    g_freed_values.clear();
    g_type_name = "System.Boolean";
}

// Caso 1 elo por elo: NENHUM pode crashar, e cada um tem que dizer por quê.
void case_each_link() {
    struct Row {
        NullAt at;
        up_resolve_status want;
        const char *label;
        const char *motivo;
    };
    const Row rows[] = {
        {NUL_FIELD, UP_RS_NO_FIELD, "class_get_field_from_name devolveu null",
         "campo não resolvido"},
        {NUL_TYPE, UP_RS_NO_TYPE, "il2cpp_field_get_type devolveu null",
         "campo sem tipo"},
        {NUL_CLASS, UP_RS_NOT_CLASS, "il2cpp_class_from_type devolveu null",
         "tipo não é classe (genérico/ponteiro/byref)"},
        {NUL_NAME, UP_RS_NO_NAME, "il2cpp_type_get_name devolveu null",
         "tipo sem nome"},
        {NUL_API_FIELD_GET_TYPE, UP_RS_NO_API, "il2cpp sem field_get_type",
         "il2cpp sem a API de tipo"},
        {NUL_API_CLASS_FROM_TYPE, UP_RS_NO_API, "il2cpp sem class_from_type",
         "il2cpp sem a API de tipo"},
        {NUL_API_TYPE_GET_NAME, UP_RS_NO_API, "il2cpp sem type_get_name",
         "il2cpp sem a API de tipo"},
    };
    for (const Row &r : rows) {
        reset();
        g_null_at = r.at;
        Il2Cpp il = make_il();
        char out[128] = "NAO TOCADO";
        // NUL_FIELD: o FieldInfo nem chegou (class_get_field_from_name
        // devolveu null). Passar &g_field seria testar outra coisa.
        void *field = (r.at == NUL_FIELD) ? nullptr : static_cast<void *>(&g_field);
        up_resolve_status st = up_resolve_field_type(&il, field, out, sizeof(out));
        printf("  -- %s\n", r.label);
        check("não crasha e recusa", st == r.want);
        check("diz o motivo certo", strcmp(up_resolve_reason(st), r.motivo) == 0);
        check("não preencheu out", strcmp(out, "NAO TOCADO") == 0);
    }
}

}  // namespace

int main() {
    printf("== u_field_nresolve_test: cadeia de resolução com Il2Cpp falso ==\n\n");

    print_case(1, "cada elo devolvendo nullptr: recusa com motivo, sem crash");
    case_each_link();

    // --- O type confusion: o que vai pro type_get_name é o TYPE -----------
    print_case(2, "type_get_name recebe o Il2CppType*, não a Il2CppClass*");
    {
        // ESTE é o caso que quebra se o bug do device voltar. O fix passa `t`;
        // o bug passava `class_from_type(t)`. Os dois são não-nulos, então
        // "não crashou" não distingue nada — só comparar o ponteiro distingue.
        reset();
        Il2Cpp il = make_il();
        char out[128] = {};
        up_resolve_status st = up_resolve_field_type(&il, &g_field, out, sizeof(out));
        check("resolução deu OK", st == UP_RS_OK);
        check("o nome saiu", strcmp(out, "System.Boolean") == 0);
        check("type_get_name recebeu o TYPE (&g_type), não a classe",
              g_type_get_name_arg == static_cast<const void *>(&g_type));
        check("e NÃO recebeu a classe (&g_class)",
              g_type_get_name_arg != static_cast<const void *>(&g_class));
        check("o nome do runtime foi solto (free chamado 1x)", g_frees == 1);
    }

    // --- O tipo de cada valor do C4, e o casamento com up_value_type_check -
    print_case(3, "tipos reais do C4: bool/int/float com want certo");
    {
        struct Row { const char *name; size_t want; bool aceita; };
        const Row rows[] = {
            {"System.Boolean", 1, true},
            {"System.Int32", 4, true},
            {"System.Single", 4, true},
            {"System.Char", 1, false},      // fora do C4 (2 bytes)
            {"System.Int64", 8, false},     // fora do C4
            {"System.String", 4, false},    // referência
        };
        for (const Row &r : rows) {
            reset();
            g_type_name = r.name;
            Il2Cpp il = make_il();
            char out[128] = {};
            up_resolve_status st = up_resolve_field_type(&il, &g_field, out, sizeof(out));
            char why[320];
            bool guard_ok = up_value_type_check(false, out, r.want, why, sizeof(why)) == 0;
            printf("  -- %s (want %zu)\n", r.name, r.want);
            check("resolução deu OK", st == UP_RS_OK);
            check("type_get_name recebeu o TYPE", g_type_get_name_arg == &g_type);
            check(r.aceita ? "guard ACEITA" : "guard RECUSA (motivo no log)",
                  guard_ok == r.aceita);
        }
    }

    // --- free ausente: sem ele o nome é do runtime e não pode ser copiado --
    //
    // ACHADO REAL (este teste, host): o código usava `free()` da libc no
    // lugar de il2cpp_free. O nome foi alocado pelo ALOCADOR DO RUNTIME, e
    // free() da libc nesse ponteiro corrompe o heap do jogo — o teste morria
    // com SIGSEGV aqui em vez de num assert. Sem il2cpp_free não dá pra copiar
    // (copiar sem devolver é leak) nem devolver (free() é UB): a resposta
    // honesta é recusar e deixar o log explicar.
    print_case(4, "free ausente: recusa em vez de free() da libc");
    {
        reset();
        Il2Cpp il = make_il();
        il.free = nullptr;   // il2cpp velho sem il2cpp_free
        char out[128] = "NAO TOCADO";
        up_resolve_status st = up_resolve_field_type(&il, &g_field, out, sizeof(out));
        check("não crasha e recusa", st == UP_RS_NO_FREE);
        check("diz o motivo", strcmp(up_resolve_reason(st),
              "il2cpp sem free (nome do runtime não pode ser copiado)") == 0);
        check("não preencheu out", strcmp(out, "NAO TOCADO") == 0);
        check("type_get_name recebeu o TYPE mesmo assim", g_type_get_name_arg == &g_type);
    }

    // --- nome maior que o buffer: recusa, não trunca ----------------------
    print_case(5, "nome de tipo maior que o buffer: recusa em vez de truncar");
    {
        reset();
        Il2Cpp il = make_il();
        char small[4];
        up_resolve_status st = up_resolve_field_type(&il, &g_field, small, sizeof(small));
        check("recusa (não trunca)", st == UP_RS_TOO_LONG);
        check("diz o motivo", strcmp(up_resolve_reason(st),
              "nome do tipo maior que o buffer") == 0);
        check("o nome do runtime foi solto mesmo assim", g_frees == 1);
    }

    // --- a linha de log, no formato que o mod escreve ----------------------
    print_case(6, "linha de log PT-BR: 'campo X não encontrado em Y'");
    {
        char line[256];
        up_resolve_log_line(UP_RS_NO_FIELD, "WeaponInfo", "unlimitedAmmo", line, sizeof(line));
        check("formato tem 'campo <cls>::<membro> não encontrado'",
              strstr(line, "campo WeaponInfo::unlimitedAmmo não encontrado") != nullptr);
        check("formato tem '— patch ignorado'", strstr(line, "— patch ignorado") != nullptr);
        check("formato cita o motivo do elo",
              strstr(line, "campo não resolvido") != nullptr);
        up_resolve_log_line(UP_RS_NOT_CLASS, "Foo", "bar", line, sizeof(line));
        check("elo genérico diz 'tipo não é classe'",
              strstr(line, "tipo não é classe") != nullptr);
        // buffer pequeno não pode estourar
        char tiny[8];
        up_resolve_log_line(UP_RS_NO_FIELD, "UmaClasseMuitoLongaDeVerdade", "campoLongo",
                            tiny, sizeof(tiny));
        check("buffer pequeno: truncado com NUL, sem estourar",
              strlen(tiny) < sizeof(tiny));
    }

    // --- args inválidos não crasham ---------------------------------------
    print_case(7, "argumentos nulos: recusa, sem crash");
    {
        reset();
        Il2Cpp il = make_il();
        char out[8] = {};
        check("il nullptr → NO_API", up_resolve_field_type(nullptr, &g_field, out, sizeof(out))
              == UP_RS_NO_API);
        check("out nullptr → NO_API", up_resolve_field_type(&il, &g_field, nullptr, 8)
              == UP_RS_NO_API);
        check("outsz 0 → NO_API", up_resolve_field_type(&il, &g_field, out, 0) == UP_RS_NO_API);
    }

    // --- a regra do device, ponta a ponta ----------------------------------
    print_case(8, "a regra que derrubou o jogo, com a cadeia toda no ar");
    {
        // `field WeaponInfo unlimitedAmmo bool true` — o texto exato da regra. O
        // arquivo no aparelho se chamava .patch na epoca do crash; a extensao
        // hoje e .bpatch e o TEXTO e o que o motor parseia.
        // do device. O parse é o mesmo de produção (up_parse_line).
        char line[] = "field WeaponInfo unlimitedAmmo bool true";
        up_rule_t r;
        check("a linha do device parseia como regra válida",
              up_parse_line(line, &r) == 0 && r.kind == UP_FIELD);
        check("classe WeaponInfo, membro unlimitedAmmo, want = 1 byte",
              strcmp(r.cls, "WeaponInfo") == 0 && strcmp(r.member, "unlimitedAmmo") == 0 &&
              r.type == UP_BOOL);
        // E agora a resolução, que era onde morria.
        // Contador LOCAL: g_fail é global e já pode estar diferente de zero por causa
        // de um caso anterior — usar o global aqui transformaria uma falha
        // alheia em "a regra do device falhou".
        int loop_bad = 0;
        for (int i = 0; i < 200; i++) {
            reset();
            Il2Cpp il = make_il();
            char out[128] = {};
            up_resolve_status st = up_resolve_field_type(&il, &g_field, out, sizeof(out));
            if (st != UP_RS_OK || g_type_get_name_arg != &g_type) loop_bad++;
        }
        check("200x a regra do device resolve sem crash e sempre com o TYPE", loop_bad == 0);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS",
           g_fail);
    return g_fail == 0 ? 0 : 1;
}
