// mechabun_mod.cpp — mod real pro bepInEx-termux: aplica o design "ideal
// comunitário" do Mecha-Bun (#426) documentado em
// battlecats-mecha-bun-ideal-comunidade.md.
//
// COMO FUNCIONA (engenharia reversa real, Ghidra 12.1 headless + radare2,
// libnative-lib.so JP 15.6.0, build-id b94cc0da...):
//   FUN_008307ec — nome dado pelo GHIDRA (endereço interno do Ghidra
//   0x8307ec, que tem offset de base +0x1000000 aplicado pelo loader do
//   Ghidra). O endereço/vaddr REAL no arquivo/binário é 0x7307ec —
//   diferença exata de 0x100000. Não afeta o mod (o hook usa busca por
//   assinatura AOB via resolve_pattern, nunca esse endereço fixo), mas
//   quem for cruzar isso manualmente com r2/objdump precisa subtrair
//   0x100000 do endereço mostrado pelo Ghidra pra achar o offset real.
//   FUN_008307ec(long bigData, int unitId) — carrega "unit%03d.csv"
//   (unitId+1, ex.: unitId=426 -> "unit427.csv") e popula uma struct
//   por unidade em bigData + (unitId+2)*0x760 + 0x9e568 — ATENCAO: o
//   offset pra ACHAR O STRUCT usa unitId+2 (achado real via decompile,
//   FUN_008307ec faz `param_2 = param_2 + 2` ANTES de multiplicar pelo
//   stride — nao e o unitId puro), enquanto o NOME DO ARQUIVO usa
//   unitId+1 — dois offsets diferentes, confirmados separadamente na
//   mesma funcao, nao confundir um com outro. Ate 4 formas (stride
//   0x1d8=472 bytes cada, `while (lVar10 != 4)` confirmado), 118
//   campos int32 por forma (offset = coluna_csv * 4, `uVar6 != 0x76`
//   onde 0x76=118 confirmado). Layout estrutural (stride/offset/
//   contagem de campo/offset+2) verificado diretamente no decompile
//   real do binário JP 15.6.0 (tmp/form_loop_result.txt).
//
// IDENTIDADE DAS COLUNAS: corrigida em cima de fonte AUTORITATIVA —
// tbcml (lib de modding Battle Cats ativa, mantida, instalada em
// ~/.venvs/battlecats-mod/), arquivo
// core/game_data/cat_base/cats.py:329-395 (`Stats.assign`). PRIMEIRA
// VERSÃO deste mod usava colunas erradas (6/7/8 pra HP, 9/10/11 pra ATK)
// baseado num doc do vault com erro de transcrição próprio (confirmado
// comparando a tabela do doc contra o CSV bruto que ele mesmo citava —
// não batia). tbcml é código executável mantido por terceiros, muito
// mais confiável que doc manuscrito — índices abaixo vêm de lá:
//   raw[0]=HP  raw[1]=knockback count  raw[2]=speed  raw[3]=ATK
//   raw[4]=attack interval  raw[5]=range  raw[6]=cost  raw[7]=recharge
//   raw[46]=wave_immunity  raw[48]=knockback_immunity
//   raw[91]=surge_immunity  raw[105]=behemoth_slayer
// (bool fields: tbcml unit_bool() = bool(value), ou seja 0=false,
// qualquer não-zero=true — setar 1 é seguro e correto.)
//
// ESCOPO — TODOS os campos abaixo confirmados via fonte autoritativa
// (tbcml unit.py/cats.py), nenhum é mais suposição rotulada:
//   - HP, ATK: multiplicar o raw por 1.8 propaga +80% pro stat final
//     computado, INDEPENDENTE da fórmula exata da curva de
//     unitlevel.csv (curva é multiplicativa sobre o raw — a mesma curva
//     aplicada a raw ou a raw*1.8 preserva a proporção 1.8x).
//   - Attack Interval, Recharge: CONFIRMADO via tbcml
//     (unit.py:126-136, `Frames.from_pair_frames`) — fórmula é
//     EXATA: frames_reais = raw * 2 ("pair frames"). Transform linear
//     provado, não suposto — escalar o raw proporcionalmente preserva a
//     proporção final exatamente.
//   - Range: CONFIRMADO via tbcml (cats.py:332, `self.range =
//     raw_data[5]`) — SEM wrapper de conversão nenhum, valor final =
//     raw direto. Escalar o raw por 250/190 dá final=250 exato.
//   - Imunidades (wave/knockback/surge) e Behemoth Slayer: bool flags
//     diretos, sem ambiguidade de escala — alta confiança, é só setar 1.
//
//   - Mini-wave (D6), Strengthen (D7), Dodge (D8): CONFIRMADO via tbcml
//     (unit.py classes Wave/Strengthen/Dodge, cats.py:assign linhas
//     349/350/362) — raw_data[35]=wave.prob, [36]=wave.level,
//     [94]=wave.is_mini; [40]=strengthen.hp_percent,
//     [41]=strengthen.multiplier_percent; [84]=dodge.prob,
//     [85]=dodge.time (frames). `Prob` é percentual direto (classe
//     `Prob`, unit.py:164-181, sem wrapper) — valores no design ideal
//     (10%, 50%, 150%, 20%, 1s=30 frames) são atribuídos direto, sem
//     conversão nenhuma precisar.
//
//   - Sage Slayer (D11), Explosion Immunity (D13): CONFIRMADO via
//     pesquisa dedicada (OpenCodePOCOC75) contra tbcml
//     (cats.py:307-330,438-449) e dados reais de outras unidades
//     (BCData unit781.csv col 111=1, unit780/784.csv col 116) — bool
//     flags diretos, mesmo padrão das outras imunidades.
//   - Warp Immunity (D14): CORRIGIDO nesta revisão — doc anterior
//     dizia "não existe campo", ERRADO. Campo real confirmado direto
//     no tbcml: `warp_blocker`, índice 75, bool flag (cats.py:227-228,
//     `self.warp_blocker = core.unit_bool(raw_data[75])`), mesmo padrão
//     das outras imunidades. Implementado.
//   - Toxic Immunity (opcional no design 3.2, baixo consenso mas
//     barato): campo real confirmado, `toxic_immunity`, índice 90,
//     bool flag (cats.py:258-259/419). Implementado.
//   - Shrug Off (D15): RESOLVIDO — pesquisa cruzada (4 fontes
//     independentes: tbcml bcu.py:176-177/547-548, BCU-java-PC
//     util.properties:364 "ot13=Dodge Attack", BCU-java-PC
//     Interpret.java:127/136, cats.py:867-920) confirma que "Shrug
//     Off" e "Dodge" são a MESMA mecânica interna — o proc `IMUATK`.
//     Não existe proc "DODGE" separado; "Shrug Off" é só o nome que a
//     comunidade deu ao mesmo proc IMUATK num contexto sem trait-alvo
//     específico. D15 = D8, já implementado via COL_DODGE_PROB/
//     COL_DODGE_TIME_FRAMES — não é aproximação, é a mesma mecânica
//     confirmada por identidade de código, não por falta de opção
//     melhor.
//
// NÃO FEITO (confirmado limitação real, não preguiça — verificado
// diretamente no tbcml, não é suposição herdada):
//   - Backswing: NÃO existe como campo CSV separado no schema real
//     (tbcml só tem "foreswing", índice 13, do attack_1) — o "backswing"
//     que a comunidade pede vem do timeline de animação
//     (mamodel/maanim), fora do escopo de um patch de memória puro.
//     Investigação dedicada (nomes de arquivo tipo "%s_entry.maanim")
//     confirma que o playback de animação é sistema de engine genérico
//     (tipo cocos2d), não dado isolado por unidade — hookar isso com
//     segurança exigiria RE de escopo comparável ou maior que o mod
//     inteiro, ainda em investigação.
//   - Ultra Form (D16): CORREÇÃO — claim anterior de "já coberto pelo
//     loop de 4 formas" estava errado. Dado real (BCData unit427.csv)
//     mostra Mecha-Bun tem só 3 formas hoje (Normal/Evolved/True); a
//     4ª iteração do loop escreve numa área que o jogo não usa/renderiza
//     pra essa unidade — não é "dar Ultra Form", é escrita inerte sem
//     efeito visível. Ultra Form de verdade exige a Ponos adicionar a
//     forma (animação/asset/stats) ao jogo — mod de patch de memória
//     não fabrica conteúdo que o jogo não tem alocado.
//   - Talents oficiais (D17): o próprio design "ideal comunitário"
//     (seção 3.3) mapeia os 10 níveis de talento propostos 1:1 pros
//     efeitos D1/D2/D3/D4/D5/D6/D7/D8/D9/D10 — TODOS já implementados
//     como stat base incondicional neste mod (sem custo de NP). Um
//     sistema de talento de verdade (hook diferente, curva de NP, save
//     de progresso) entregaria o MESMO efeito, só que atrás de grind —
//     pior pro jogador que o que já existe. Não implementado por não
//     ter ganho real, não por limitação técnica.
//
// Mecha-Bun = unitId 426 (zero-based; unit427.csv = unitId+1=427).
//
// CORRECAO CRITICA (achado real desta revisao, cross-referenciado
// contra BCData real): a sessao inteira usava unitId=425 (unit426.csv)
// por engano. unit426.csv/Unit_Explanation427_en.csv sao unidades
// DIFERENTES sem relacao com Mecha-Bun (unit426.csv = "太秦鴻＆ネコ",
// um personagem escolar). O nome real "Mecha-Bun" so aparece em
// Unit_Explanation427_en.csv -> unitId zero-based = 426 -> unit427.csv.
// Confirmado por dado real: unit427.csv raw[5] (range) = 190, batendo
// EXATO com a referencia de design "Range 190->250" usada pelo mod —
// prova forte de que 426 e o ID certo e 425 nunca foi.
//
// ACHADO CRITICO 2026-09-20 (investigacao /loop, r2 direto no binario do
// device + Ghidra via OpenCode, NAO suposicao) — ESTE STRUCT NAO E O QUE A
// BATALHA REAL LE PRA ATK/RANGE/RECHARGE:
//   Confirmado via busca exaustiva de xrefs (padrao mov+movk imediato
//   completo, unico jeito que C++ compilado acessa campo de struct fixo):
//   ATK (+0x9e324), RANGE (+0x9e32c), RECHARGE (+0x9e334), STRONG_AGAINST
//   (+0x9e374), BEHEMOTH_SLAYER (+0x9e4bc), SAGE_SLAYER (+0x9e4d4),
//   WAVE_IS_MINI (+0x9e490), TOXIC_IMMUNITY (+0x9e480) tem ZERO
//   instrucoes no binario inteiro que leem esses enderecos. Confirmado
//   NAO ser blind-spot de metodo: HP (+0x9e318, MESMO struct) tem 5
//   leitores reais (0x8712d8/0x871420/0x872290/0x8725c0/0x87271d, fazendo
//   multiplicacao/divisao de ponto fixo) achados com a MESMA tecnica —
//   ou seja, quando existe consumidor real, a busca acha. A auséncia
//   pros campos acima é conclusão sólida, não falha de busca.
//   OpenCode tracou a cadeia de spawn de batalha (Ghidra): factory de
//   entidade de combate fcn.008717f8 (aloca objeto de 0x1e8=488 bytes)
//   NUNCA referencia esse struct (zero 0x760/0x1d8/0x9exx no corpo dela)
//   — recebe tudo por argumento vindo de uma camada de "prep" (8+ call
//   sites em 0x7c3a38-0x7c3e4c) que por sua vez le de uma tabela
//   COMPLETAMENTE DIFERENTE: singleton (bl 0x601f5c) + idx*0xc738 +
//   idx2*0x3e8 + offset fixo por campo (achados: +0x83924, +0x83978,
//   +0x8397c, +0x83a54, +0x83a58, +0x83a7c, via dezenas de thunks
//   getter/setter auto-gerados em 0x870910+). ESSA e provavelmente a
//   struct de "stat efetivo de batalha" (pos-talento/tesouro/catseye)
//   que o dano real consulta — mas qual offset especifico = ATK ainda
//   NAO foi identificado (precisa de device ao vivo: hookar os STR
//   nesses offsets durante boot de batalha real e comparar valor contra
//   ATK conhecido=400 pra identificar por eliminacao).
//   RESOLVIDO parcialmente sem device (sessao Ghidra q12-q19): campos de
//   dano sao +0x838c8/+0x838f8 (setters fileoff 0x86ac88/0x86bfd0, unicos
//   writers, alimentados pelos getters fileoff 0x8782c4/0x878cec) — MAS
//   esses 2 getters foram depois provados condicionais (gated por flag de
//   warp/curse), nao o caminho do dano normal. 0x8789e8 -> +0x83774
//   (col26), hookeado ate 2026-09-23 como "D2-fix", e' o getter de
//   FREEZE TIME, nao dano (hook removido). Dano normal: calc_atk 0x872440
//   le col3 deste struct via tabela de colunas 0x1f8310 (build 338b0601).
//
//   IMPACTO PRATICO NO MOD ATUAL: os campos abaixo SAO ESCRITOS
//   corretamente neste struct (offset e valor confirmados via log ao
//   vivo), MAS NAO TEM EFEITO EM BATALHA REAL, root cause confirmado
//   nao so hipotese (rebaixar a confianca dos comentarios "alta
//   confianca" mais abaixo pra esses campos especificos):
//     - ATK (D2 parcial): +80% no struct, ZERO efeito no dano real.
//     - RANGE (D3): 190->250 no struct, ZERO efeito no alcance real.
//     - RECHARGE (D5): no struct, ZERO efeito no cooldown real.
//     - STRONG_AGAINST (D16 Relic), BEHEMOTH_SLAYER (D9), SAGE_SLAYER
//       (D11), WAVE_IS_MINI (parte do D6), TOXIC_IMMUNITY (opcional):
//       flag setada no struct, ZERO leitor -- feature nao aplicada.
//   Campos com leitor confirmado (via round6 anterior + esta sessao) e
//   que plausivelmente FUNCIONAM: HP (D2/D16 Ultra parcial), demais
//   imunidades bool (wave/knockback/surge/warp), dodge, wave_prob,
//   wave_level, strengthen — continuam "alta confianca" como já
//   documentado.
//   Fix real pendente (fora do escopo desta revisao, documentado pra
//   proxima sessao com device): localizar o offset exato de ATK na
//   tabela 0x839xx via correlacao ao vivo, e adicionar um SEGUNDO hook
//   (ou hook no ponto de populacao dessa tabela) escrevendo o valor
//   escalado la tambem — sem isso, patch de ATK/RANGE/RECHARGE/3
//   traits fica permanentemente inerte em batalha real, nao importa
//   quao correto o offset deste struct estiver.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include "bc_mod_api.h"

// pthread_mutex direto em vez de std::mutex/<mutex> -- APP_STL=c++_static
// (jni/Application.mk) faz o linker puxar boa parte do runtime C++ estatico
// so por causa do <mutex> (mutex/lock_guard usam machinery de threading da
// libc++ que nao eh trivialmente stripavel), inchando o .so de ~9KB pra
// ~333KB so com essa unica inclusao (medido nesta sessao). pthread.h eh
// puro C, sempre presente na libc do NDK, sem esse custo.
struct PthreadMutexGuard {
    explicit PthreadMutexGuard(pthread_mutex_t *m) : mutex_(m) {
        pthread_mutex_lock(mutex_);
    }
    ~PthreadMutexGuard() { pthread_mutex_unlock(mutex_); }
    PthreadMutexGuard(const PthreadMutexGuard &) = delete;
    PthreadMutexGuard &operator=(const PthreadMutexGuard &) = delete;
    pthread_mutex_t *mutex_;
};

#define MECHABUN_UNIT_ID 426
#define FORM_STRIDE 0x1d8
#define UNIT_BLOCK_STRIDE 0x760
// 0x9e318 -- CORRIGIDO nesta revisao. Valor antigo (0x9e568, diferenca de
// 0x250 bytes) foi RE'd contra o binario JP 15.6.0 build-id
// b94cc0dafd8521f1f7cfcf3841a29f13d7cd1ef3; o binario rodando no device
// de teste (hash md5 1f9bb61e..., NAO bate com nenhum dos dumps de
// referencia jp/en salvos no repo) e' um build diferente -- update do
// jogo moveu esse offset global especifico, mantendo tudo mais
// identico (function AOB, +2, stride 0x760, form stride 0x1d8).
// Confirmado via disassembly direto do .so real do device (r2), nao
// suposicao: `mov w11,0xe318 / movk w11,9,lsl16` = 0x9e318 exato.
// Causa raiz do bug "ATK lendo 0" + crash em batalha (escrita
// corrompendo memoria vizinha). Reverifica isso a cada update do jogo.
#define STAT_BLOCK_OFF 0x9e318
#define N_FORMS 4
// FUN_008307ec faz `param_2 = param_2 + 2` ANTES de multiplicar pelo
// stride pra achar o endereco do struct (confirmado via decompile real,
// tmp/form_loop_result.txt linha 70-72) — o indice usado pra
// enderecamento NAO e o unitId puro, e unitId+2 (provavel reserva de 2
// slots no inicio da tabela). O filename ("unit%03d.csv") usa
// unitId+1, endereco do struct usa unitId+2 — dois offsets DIFERENTES,
// nao confundir.
#define UNIT_ID_TABLE_OFFSET 2

// Índices reais (tbcml cats.py:assign), NÃO os antigos e errados.
#define COL_HP 0
#define COL_ATK 3
#define COL_ATTACK_INTERVAL 4
#define COL_RANGE 5
#define COL_RECHARGE 7
#define COL_WAVE_IMMUNITY 46
#define COL_KNOCKBACK_IMMUNITY 48
#define COL_SURGE_IMMUNITY 91
#define COL_BEHEMOTH_SLAYER 105
#define COL_WAVE_PROB 35
#define COL_WAVE_LEVEL 36
#define COL_WAVE_IS_MINI 94
#define COL_STRENGTHEN_HP_PERCENT 40
#define COL_STRENGTHEN_MULT_PERCENT 41
#define COL_DODGE_PROB 84
#define COL_DODGE_TIME_FRAMES 85
#define COL_SAGE_SLAYER 111
#define COL_EXPLOSION_IMMUNITY 116
#define COL_WARP_IMMUNITY 75
#define COL_TOXIC_IMMUNITY 90
#define COL_TARGET_FLOATING 16
#define COL_TARGET_AKU 96
#define COL_STRONG_AGAINST 23
#define COL_KB_COUNT 1
#define COL_FREEZE_PROB 25
#define COL_FREEZE_TIME 26
#define COL_CRIT_PROB 31
#define COL_WEAKEN_PROB 37
#define COL_WEAKEN_TIME 38
#define COL_WEAKEN_PERCENT 39
#define COL_SURVIVE_PROB 42
#define COL_FREEZE_IMMUNITY 49
#define COL_SLOW_IMMUNITY 50
#define COL_WEAKEN_IMMUNITY 51
#define COL_TARGET_RED 10
#define COL_TARGET_BLACK 17
#define COL_TARGET_METAL 18
#define COL_TARGET_TRAITLESS 19
#define COL_TARGET_ANGEL 20
#define COL_TARGET_ALIEN 21
#define COL_TARGET_ZOMBIE 22
#define COL_RESISTANT 29
#define COL_MASSIVE_DAMAGE 30
#define COL_COLOSSUS_SLAYER 97
#define COL_SOUL_STRIKE 98
#define TRUE_FORM_INDEX 2

typedef void (*orig_load_unit_fn)(long big_data, int unit_id);
static orig_load_unit_fn g_orig = nullptr;
static const bc_mod_api *g_api = nullptr;

// Assinatura AOB do prólogo de FUN_008307ec (48 bytes, ÚNICA no binário
// JP 15.6.0 inteiro — verificado via busca exaustiva de bytes no .so
// completo, contagem exata = 1).
static const uint8_t kPattern[] = {
    0xe8, 0x0f, 0x19, 0xfc, 0xfd, 0x7b, 0x01, 0xa9, 0xfc, 0x6f, 0x02, 0xa9,
    0xfa, 0x67, 0x03, 0xa9, 0xf8, 0x5f, 0x04, 0xa9, 0xf6, 0x57, 0x05, 0xa9,
    0xf4, 0x4f, 0x06, 0xa9, 0xfd, 0x43, 0x00, 0x91, 0xff, 0x03, 0x07, 0xd1,
    0x48, 0xd0, 0x3b, 0xd5, 0xf3, 0x03, 0x00, 0xaa, 0xe0, 0x63, 0x01, 0x91,
};
static const uint8_t kMask[sizeof(kPattern)] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static inline int32_t *field_ptr(long form_base, int col) {
    return reinterpret_cast<int32_t *>(form_base + (long)col * 4);
}

// Multiplica um campo por num/den (inteiro exato, sem float).
static void scale_field(long form_base, int col, int64_t num, int64_t den) {
    int32_t *p = field_ptr(form_base, col);
    *p = (int32_t)(((int64_t)*p * num) / den);
}

static void set_bool_field(long form_base, int col) {
    *field_ptr(form_base, col) = 1;
}

// Atribui valor direto (percentual/frames já na unidade certa, sem
// escala — Prob e Frames do tbcml não têm wrapper de conversão).
static void set_field(long form_base, int col, int32_t value) {
    *field_ptr(form_base, col) = value;
}

static std::atomic<int> g_mechabun_call_count{0};

// Assinatura de sanidade contra unit427.csv real (raw, PRE-escala) --
// STAT_BLOCK_OFF ja driftou uma vez entre builds do jogo (0x9e568 ->
// 0x9e318, ver comentario acima) e o mod escreveu cego no endereco errado:
// resultado foi ATK lendo 0 e SIGSEGV em batalha (memoria vizinha
// corrompida). Essas 3 constantes sao valores raw confirmados por leitura
// direta do CSV real (DEBUG log de sessao anterior + comentario da linha
// 132): range raw=190 (forma 0), ATK raw=400 (formas 0/1), ATK raw=500
// (True Form). Playbook agora: LE antes de ESCREVER, confere contra esses
// 3 valores independentes (colisao aleatoria virtualmente impossivel), e so
// entra no loop de patch se todos baterem. Se um update futuro do jogo
// mover STAT_BLOCK_OFF de novo, o mod detecta aqui, loga o mismatch com os
// valores reais vistos (pista pronta pra proxima verificacao via r2) e
// DESISTE sem tocar em nenhum byte -- pior caso vira "stats vanilla nesse
// boot", nunca mais "corrompe memoria e derruba o jogo em batalha".
#define EXPECTED_RANGE_RAW_FORM0 190
#define EXPECTED_ATK_RAW_FORM0 400
#define EXPECTED_ATK_RAW_TRUE_FORM 500

static bool verify_unit_base(long unit_base, const bc_mod_api *api) {
    int32_t range0 = *field_ptr(unit_base, COL_RANGE);
    int32_t atk0 = *field_ptr(unit_base, COL_ATK);
    long fb_true = unit_base + (long)TRUE_FORM_INDEX * FORM_STRIDE;
    int32_t atk_true = *field_ptr(fb_true, COL_ATK);

    // Range: device real (build 338b0601, 2026-09-22) mostrou 760 = 190*4
    // com ATK 400/500 batendo exato -- offset certo, o struct guarda range
    // em unidade interna x4. Aceita as 2 formas; o par de ATK segue sendo a
    // prova de offset. Escala 250/190 e' proporcional, vale nas duas.
    bool ok = (range0 == EXPECTED_RANGE_RAW_FORM0 ||
               range0 == EXPECTED_RANGE_RAW_FORM0 * 4) &&
              atk0 == EXPECTED_ATK_RAW_FORM0 &&
              atk_true == EXPECTED_ATK_RAW_TRUE_FORM;
    if (!ok && api != nullptr && api->log != nullptr) {
        char buf[448];
        snprintf(buf, sizeof(buf),
                 "[mechabun] ERRO CRITICO: STAT_BLOCK_OFF (0x%x) nao bate "
                 "mais com unit427.csv real -- esperado range=%d/atk=%d/"
                 "atkTrue=%d, visto range=%d/atk=%d/atkTrue=%d. Jogo deve "
                 "ter atualizado e movido o offset (ver comentario do "
                 "STAT_BLOCK_OFF pra reverificar via r2). Patch NAO "
                 "aplicado nesse boot -- Mecha-Bun fica com stats vanilla, "
                 "sem crash.",
                 (unsigned)STAT_BLOCK_OFF, EXPECTED_RANGE_RAW_FORM0,
                 EXPECTED_ATK_RAW_FORM0, EXPECTED_ATK_RAW_TRUE_FORM, range0,
                 atk0, atk_true);
        api->log(BC_LOG_ERROR, buf);
    }
    return ok;
}

// Teto de nivel 60+90 (unitbuy.csv em memoria, sem tocar arquivo).
// freebuff 2026-09-23, build 338b0601: parse 0x7936b8 grava cada linha em
// this+0x4ACB8+unit*0x100 (this = mesmo singleton do big_data), dword
// col*4 XOR key de 4 bytes em row+0xfc. Leitores: col49 (check de upgrade
// 0x3e85f8 = col49 + bonus catseye do save), col50 0x3e7f7c, col51 0x3e80d8,
// col18/19 getter 0x384e08. Vanilla EN visto no device: col49=30,
// col51=0 (BCData 14.7 dizia 70) -- confere antes de escrever, senao desiste.
#define UNITBUY_TABLE_OFF 0x4ACB8
#define UNITBUY_ROW_STRIDE 0x100
#define UNITBUY_KEY_OFF 0xfc

static uint32_t *unitbuy_field(long row, int col) {
    return reinterpret_cast<uint32_t *>(row + (long)col * 4);
}

static void patch_level_caps(long big_data) {
    static bool done = false;
    if (done) return;
    long row = big_data + UNITBUY_TABLE_OFF + (long)MECHABUN_UNIT_ID * UNITBUY_ROW_STRIDE;
    uint32_t key = *reinterpret_cast<uint32_t *>(row + UNITBUY_KEY_OFF);
    uint32_t c49 = *unitbuy_field(row, 49) ^ key;
    uint32_t c50 = *unitbuy_field(row, 50) ^ key;
    uint32_t c51 = *unitbuy_field(row, 51) ^ key;
    char buf[200];
    snprintf(buf, sizeof(buf),
             "[mechabun] unitbuy 426 vanilla: col18=%u col19=%u col49=%u col50=%u col51=%u",
             *unitbuy_field(row, 18) ^ key, *unitbuy_field(row, 19) ^ key, c49, c50, c51);
    g_api->log(BC_LOG_INFO, buf);
    if (c49 != 30 || (c51 != 0 && c51 != 70)) {
        snprintf(buf, sizeof(buf),
                 "[mechabun] teto de nivel NAO aplicado: unitbuy col49=%u col51=%u "
                 "(esperado 30 e 0|70)", c49, c51);
        g_api->log(BC_LOG_WARN, buf);
        return;
    }
    const struct { int col; uint32_t v; } caps[] = {
        {18, 60}, {19, 90}, {49, 60}, {50, 90}, {51, 90},
    };
    for (const auto &c : caps) *unitbuy_field(row, c.col) = c.v ^ key;
    done = true;
    g_api->log(BC_LOG_INFO, "[mechabun] teto de nivel aplicado (unitbuy 426: 60+90)");
}

static void hooked_load_unit(long big_data, int unit_id) {
    g_orig(big_data, unit_id);  // deixa o parse original do CSV rodar
    if (unit_id != MECHABUN_UNIT_ID) return;

    g_mechabun_call_count++;
    if (g_api != nullptr) {
        char callbuf[96];
        snprintf(callbuf, sizeof(callbuf),
                 "[mechabun] DEBUG call#%d big_data=0x%lx",
                 g_mechabun_call_count.load(), (unsigned long)big_data);
        g_api->log(BC_LOG_INFO, callbuf);
    }

    long unit_base = big_data +
        (long)(unit_id + UNIT_ID_TABLE_OFFSET) * UNIT_BLOCK_STRIDE +
        STAT_BLOCK_OFF;
    if (g_api != nullptr) patch_level_caps(big_data);
    if (!verify_unit_base(unit_base, g_api)) return;
    for (int form = 0; form < N_FORMS; form++) {
        long fb = unit_base + (long)form * FORM_STRIDE;

        // Alta confiança: proporcional preserva o resultado final
        // independente da fórmula da curva de level.
        // True Form: alvo 300.000 HP Lv50 (decisao do usuario 2026-09-23).
        // Raw TF = 4000 -> Lv50 vanilla 108.000 (mesmo x27 de 3200 ->
        // 86.400 das formas 0/1); 300000/108000 = 25/9. Versao anterior
        // usava 325/54 contra 86.400 (base da forma errada) = ~650k real.
        if (form == TRUE_FORM_INDEX) {
            scale_field(fb, COL_HP, 25, 9);    // 300.000 HP Lv50
        } else {
            scale_field(fb, COL_HP, 9, 5);     // +80% (D2)
        }
        int32_t atk_before = *field_ptr(fb, COL_ATK);
        scale_field(fb, COL_ATK, 9, 5);    // +80%
        int32_t atk_after = *field_ptr(fb, COL_ATK);
        if (g_api != nullptr) {
            char dbgbuf[96];
            snprintf(dbgbuf, sizeof(dbgbuf),
                     "[mechabun] DEBUG form=%d ATK raw=%d pos-scale=%d",
                     form, atk_before, atk_after);
            g_api->log(BC_LOG_INFO, dbgbuf);
        }

        // Confirmado via tbcml (não suposição): range sem transform,
        // recharge/attack_interval = raw*2 exato (pair frames).
        scale_field(fb, COL_RANGE, 265, 190);        // 190 -> 265 (leitor 0x872cc0)
        scale_field(fb, COL_RECHARGE, 2136, 2536);   // 2536f -> 2136f (-400f)
        // NAO-OP para o Mecha-Bun: COL_ATTACK_INTERVAL (col4) e' o mesmo
        // campo que a pesquisa D12 chama de TBA, e o raw do unit427.csv
        // e' 0 -- 0*26/32=0, escala nao muda nada. Mantido (inofensivo)
        // só pra unit hipotético com TBA>0. D12 (ciclo 32f->26f via edicao
        // do .maanim) foi tentado via redirect de pack e ABANDONADO -- ver
        // nota mais abaixo (hooked_fopen) com o motivo. Essa linha nao
        // entrega D12 sozinha.
        scale_field(fb, COL_ATTACK_INTERVAL, 26, 32);

        // Alta confiança: bool flags diretos.
        set_bool_field(fb, COL_WAVE_IMMUNITY);
        set_bool_field(fb, COL_KNOCKBACK_IMMUNITY);
        set_bool_field(fb, COL_SURGE_IMMUNITY);
        set_bool_field(fb, COL_BEHEMOTH_SLAYER);
        set_bool_field(fb, COL_SAGE_SLAYER);        // D11
        set_bool_field(fb, COL_EXPLOSION_IMMUNITY); // D13
        set_bool_field(fb, COL_WARP_IMMUNITY);      // D14
        set_bool_field(fb, COL_TOXIC_IMMUNITY);     // opcional (design ideal 3.2)

        // D16 Ultra (traits): Strong Against Floating/Relic/Aku. Loop
        // aplica nas 3 formas (0/1/2), nao so True Form -- diferenca do
        // README (que descreve so o alvo True Form) e' documentacao
        // desatualizada, nao bug: dar o trait nas formas fracas tambem
        // e' estritamente um buff a mais, sem efeito colateral negativo.
        // Mecha-Bun já nasce com strong=1 e target_relic=1 nativos na
        // True Form (confirmado no raw real, unit427.csv linha 3) — só
        // faltam target_floating e target_aku pra completar os 3 traits.
        set_bool_field(fb, COL_STRONG_AGAINST);
        set_bool_field(fb, COL_TARGET_FLOATING);
        set_bool_field(fb, COL_TARGET_AKU);

        // D6 Onda: 30% chance, nível 1, onda cheia (regra do usuario:
        // proposta 20-30% -> 30%; onda cheia > mini).
        set_field(fb, COL_WAVE_PROB, 30);
        set_field(fb, COL_WAVE_LEVEL, 1);
        set_field(fb, COL_WAVE_IS_MINI, 0);

        // D7 Strengthen: ativa a 50% de HP, +50% de dano (total 150%).
        // ATENCAO (achado de review, historico real no CHANGELOG): o campo
        // COL_STRENGTHEN_MULT_PERCENT e' BONUS-percentual (cf. tbcml
        // Strengthen.multiplier_percent), NAO o total -- por isso o valor
        // abaixo fica em 50, nunca 150. Uma versao anterior deste mod usou
        // 150 achando que era "dano total", e isso over-buffou 2.5x na
        // pratica (ja corrigido, ver CHANGELOG "Changed"). Nao reverter.
        set_field(fb, COL_STRENGTHEN_HP_PERCENT, 50);
        set_field(fb, COL_STRENGTHEN_MULT_PERCENT, 50);

        // D8 Dodge: 50% de chance (regra do usuario), esquiva por 3s (90f).
        set_field(fb, COL_DODGE_PROB, 50);
        set_field(fb, COL_DODGE_TIME_FRAMES, 90);

        // Leva 2026-09-23 (freebuff: leitor em batalha provado por coluna,
        // build 338b0601). Valores = maior citado na pesquisa comunitaria
        // (context/mecha-bun-extracao-completa.md), regra do usuario.
        set_field(fb, COL_KB_COUNT, 4);
        set_field(fb, COL_FREEZE_PROB, 20);
        set_field(fb, COL_FREEZE_TIME, 90);
        set_field(fb, COL_CRIT_PROB, 25);
        set_field(fb, COL_WEAKEN_PROB, 100);
        set_field(fb, COL_WEAKEN_TIME, 120);    // 4s
        set_field(fb, COL_WEAKEN_PERCENT, 50);
        set_field(fb, COL_SURVIVE_PROB, 100);
        set_bool_field(fb, COL_FREEZE_IMMUNITY);
        set_bool_field(fb, COL_SLOW_IMMUNITY);
        set_bool_field(fb, COL_WEAKEN_IMMUNITY);

        // Leva 2 (regra do usuario: inclui propostas contestadas/rejeitadas).
        // Leitores ainda a provar (maestri), sem efeito colateral se inertes.
        set_bool_field(fb, COL_TARGET_RED);
        set_bool_field(fb, COL_TARGET_BLACK);
        set_bool_field(fb, COL_TARGET_METAL);
        set_bool_field(fb, COL_TARGET_TRAITLESS);
        set_bool_field(fb, COL_TARGET_ANGEL);
        set_bool_field(fb, COL_TARGET_ALIEN);
        set_bool_field(fb, COL_TARGET_ZOMBIE);
        set_bool_field(fb, COL_RESISTANT);
        set_bool_field(fb, COL_MASSIVE_DAMAGE);
        set_bool_field(fb, COL_COLOSSUS_SLAYER);
        set_bool_field(fb, COL_SOUL_STRIKE);
    }
    if (g_api != nullptr) {
        g_api->log(BC_LOG_INFO,
                        "[mechabun] design ideal comunitario aplicado (HP/ATK "
                    "+80% formas 0/1, True Form 300k Lv50; 9 imunidades, "
                    "crit/weaken/freeze/survive/KB4, onda 30%, 10 traits, resistant/massive/colossus/soulstrike, "
                    "strengthen, dodge 50%)");
    }
}

// D2-fix REMOVIDO (2026-09-23): o hook em 0x8789e8 era o getter de
// freeze time (col26 + talento), nao dano -- escalava o freeze 9/5.
// Dano real passa por calc_atk 0x872440 (le col3 do struct); ver
// CHANGELOG.

// D16.1 — redirect de deploy/upgrade icon (fopen hook, mod-only, zero
// arquivo do jogo tocado). fopen é símbolo importado real (PLT), ABI
// estável (const char*, const char*), risco muito menor que decifrar
// std::string de libc++ dentro de TextureCache::loadAsync (ver README
// seção D16.1 pra essa cadeia descartada por risco). Filtro por
// substring simples no path — se não bater, chama fopen original sem
// nenhuma modificação; hot-path (fopen é chamado o tempo todo pelo
// jogo inteiro), mas o filtro em si é uma comparação de string barata
// e sem estado, sem parsing de struct opaca.
typedef FILE *(*orig_fopen_fn)(const char *path, const char *mode);
static orig_fopen_fn g_orig_fopen = nullptr;

#define MECHABUN_ASSET_DIR "/data/local/tmp/bc_mods/mechabun_assets/"

// D12 v2 — redirect do pack de animacao (fopen hook, extensao do D16.1).
// v1 (425_f01.maanim) foi ERRADO: unit 425 nunca foi a Mecha-Bun (rig
// humanoide de saia/camisa, unit nao identificada). Corrigido apos
// extrair e comparar visualmente o icon REAL (uni426_s00.png, ja usado
// no D16.1) contra o mamodel completo de 426_s: partes "018/019/020
// ネコ座席" (banco do gato), "044ジェット"/"045背中羽" (jato/asa-jato nas
// costas), "008/009ネコ/ネコ顔" (gato/rosto de gato), "011ゴーグル"
// (oculos), "016コントローラー棒" (manche) -- bate exato com a armadura
// dourada + gato piloto do icon. unit correto = 426, forma correta = 's'
// (True Form). Dentro das 4 anims de 426_s (s00-s03), s01 e' o ataque:
// unica com o sprite "009攻撃胸S.png" ('攻撃' = ataque, literal) mais
// movimento de punho/manche. Pack redirecionado byte-identico ao
// original exceto os 1984 bytes cifrados de 426_s01.maanim (offset
// 33116896) -- keyframes escalados 26/32 (mesma razao do ciclo de
// ataque 32f->26f pedido pela comunidade), reencriptado na MESMA janela
// (offset/tamanho do .list intocados). Historico completo (incl. os 2
// erros anteriores) em context/battlecats-d12-backswing-pesquisa.md.
#define MECHABUN_D12_PACK_NAME "ImageDataServer_100600_00_en.pack"
// Byte-window patch preserva o tamanho original do pack exatamente (so a
// janela cifrada de 426_s01 muda de conteudo, nunca de tamanho) -- entao
// um push incompleto/corrompido produz um arquivo de tamanho diferente.
// Checagem de tamanho (nao hash) e suficiente pra pegar isso e barata;
// achado de review rodada 4 (devin: pack corrompido abria como FILE*
// valido sem deteccao).
#define MECHABUN_D12_PACK_EXPECTED_SIZE 57367424L

static FILE *hooked_fopen(const char *path, const char *mode) {
    if (path != nullptr) {
        // achado de review forense: os 2 redirects de icon abaixo faziam
        // return incondicional do fopen redirecionado, sem o mesmo
        // fallback-pro-original que o pack (abaixo) ja tinha (achado de
        // review rodada 2) -- se o icon custom nao foi deployado no
        // device, isso retornava NULL pro jogo (fopen falha), em vez de
        // cair pro fopen(path, mode) original no fim da funcao.
        if (strstr(path, "uni426_s00.png") != nullptr) {
            FILE *f = g_orig_fopen(MECHABUN_ASSET_DIR "uni426_s00.png", mode);
            if (f != nullptr) return f;
        } else if (strstr(path, "udi426_s.png") != nullptr) {
            FILE *f = g_orig_fopen(MECHABUN_ASSET_DIR "udi426_s.png", mode);
            if (f != nullptr) return f;
        } else if (strstr(path, MECHABUN_D12_PACK_NAME) != nullptr) {
            // Fallback: se o pack modificado nao foi deployado no device,
            // usa o original -- nunca retorna NULL pro jogo (achado de
            // review, rodada 2).
            FILE *f = g_orig_fopen(MECHABUN_ASSET_DIR MECHABUN_D12_PACK_NAME, mode);
            if (f != nullptr) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (sz == MECHABUN_D12_PACK_EXPECTED_SIZE) {
                    static std::atomic<bool> s_logged_once{false};
                    if (!s_logged_once.exchange(true) && g_api != nullptr) {
                        g_api->log(BC_LOG_INFO,
                                   "[mechabun] D12: redirect do pack de animacao "
                                   "(426_s01, ciclo 32f->26f)");
                    }
                    return f;
                }
                fclose(f);
                if (g_api != nullptr) {
                    g_api->log(BC_LOG_WARN,
                               "[mechabun] D12: pack modificado com tamanho "
                               "errado (push incompleto?), usando original");
                }
            } else if (g_api != nullptr) {
                g_api->log(BC_LOG_WARN,
                           "[mechabun] D12: pack modificado ausente no "
                           "device, usando original (sem redirect)");
            }
        }
    }
    return g_orig_fopen(path, mode);
}

// D16.1 + D12 compartilham este hook: instala fopen redirect, com log de
// sucesso/falha. Extraido pra funcao pra poder ser retentado no reload
// (achado de review rodada 4: guard de idempotencia so olhava g_orig,
// nunca reinstalava fopen se so ele tivesse falhado no 1o registro).
static void try_install_fopen_hook(const bc_mod_api *api) {
    if (api->resolve_symbol == nullptr) return;
    void *fopen_target = api->resolve_symbol("fopen");
    if (fopen_target != nullptr &&
        api->install_hook(fopen_target, (void *)hooked_fopen,
                           (void **)&g_orig_fopen)) {
        api->log(BC_LOG_INFO,
                   "[mechabun] D16.1: hook de fopen instalado (redirect "
                   "de deploy/upgrade icon do Mecha-Bun)");
    } else {
        api->log(BC_LOG_WARN,
                   "[mechabun] D16.1: fopen nao resolvido/hookado - "
                   "icons custom nao vao aparecer, resto do mod segue ok");
    }
}

// Crash real em device (2026-09-21, bc_mod_register+32, SEGV_ACCERR numa
// thread nova via pthread) — 2 agentes (devin, hermes) convergiram
// independentemente na causa: o guard "if (g_orig != nullptr)" abaixo eh
// TOCTOU sem lock. Se bc_mod_register for chamado quase ao mesmo tempo de
// 2 threads (load normal do zygote + sinal de reload do companion), as
// duas podem ver g_orig==nullptr, entrar no caminho de 1a instalacao, e
// chamar install_hook (DobbyHook) CONCORRENTE no MESMO alvo -- Dobby faz
// mprotect RWX na pagina de codigo sem lock proprio, e 2 toggles
// concorrentes na mesma pagina produzem exatamente SEGV_ACCERR (erro de
// PROTECAO de memoria, nao null-deref -- bate com o crash observado).
// Fix: mutex cobrindo a funcao inteira, nao soh as variaveis globais
// individualmente (atomics nao resolvem o check-then-act do guard).
static pthread_mutex_t g_register_mutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" BC_MOD_EXPORT bool bc_mod_register(const bc_mod_api *api) {
    PthreadMutexGuard lock(&g_register_mutex);
    // Idempotent: hot-reload (reload_mods property → load_dynamic_mods() again)
    // calls this on the same dlopen'd handle. DobbyHook on an already-hooked
    // target has no dedup → g_orig trampoline is overwritten, creating
    // infinite recursion in hooked_load_unit/hooked_fopen → stack overflow.
    // Guard: if hooks are already installed (g_orig non-null), exit early.
    // This is safe because g_orig is only set after a successful install_hook.
    // Retry so fopen alone (achado rodada 4): se o load_unit hook pegou mas
    // o fopen hook falhou no 1o registro (g_orig_fopen ainda null), o
    // proximo reload tenta so essa parte -- sem re-tocar o hook principal
    // ja instalado (evitaria double-hook/recursao, ver comentario acima).
    if (g_orig != nullptr) {
        if (g_orig_fopen == nullptr && api != nullptr && api->log != nullptr) {
            try_install_fopen_hook(api);
        }
        if (api != nullptr && api->log != nullptr) {
            api->log(BC_LOG_INFO,
                       "[mechabun] ja registrado (hot-reload) — hooks preservados");
        }
        return true;
    }

    if (api != nullptr && api->log != nullptr) {
        char diagbuf[128];
        snprintf(diagbuf, sizeof(diagbuf),
                 "[diag] version=%u install_hook=%p resolve_pattern=%p",
                 api->version, (void *)api->install_hook,
                 (void *)api->resolve_pattern);
        api->log(BC_LOG_INFO, diagbuf);
    }
    if (api == nullptr || api->version < 3 || api->install_hook == nullptr ||
        api->resolve_pattern == nullptr) {
        return false;  // loader antigo sem install_hook — mod fica inativo, não crasha
    }
    g_api = api;
    void *target = api->resolve_pattern(kPattern, kMask, sizeof(kPattern));
    if (target == nullptr) {
        api->log(BC_LOG_WARN,
                  "[mechabun] assinatura do loader de unit CSV nao achada "
                  "(build do jogo mudou?) - mod fica inativo");
        return false;
    }
    bool ok = api->install_hook(target, (void *)hooked_load_unit,
                                 (void **)&g_orig);
    if (!ok) {
        api->log(BC_LOG_ERROR, "[mechabun] install_hook falhou");
        return false;
    }
    api->log(BC_LOG_INFO, "[mechabun] hook instalado, aguardando load do unit_id 426 (unit427.csv)");

    // D16.1 icon redirect: opcional, não derruba o mod principal se
    // falhar (resolve_symbol pode não existir em loader mais antigo,
    // ou fopen pode não ser a função real usada pelo loader de
    // textura — ver README D16.1 pra ressalvas). Instala só se tudo
    // bater; senão loga e segue com o hook de stats já garantido.
    try_install_fopen_hook(api);
    return true;
}
