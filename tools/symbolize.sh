#!/usr/bin/env bash
# tools/symbolize.sh — crash report do usuário vira função:linha, em 1 comando.
#
#   tools/symbolize.sh <tombstone> [nome-do-binário]
#   tools/symbolize.sh --offset <build-id> <0x1bb34> [nome]
#
# Lê o build-id e os offsets do backtrace do tombstone, acha o .so não-stripado
# correspondente em symbols/ e imprime, por frame:
#
#   #04  0x1bb34  libbc-poc.so  up_field_type_name  u_patch_mod.cpp:375
#
# REGRA: um resultado só sai sem ressalva quando o build-id do tombstone casou
# com o do símbolo guardado. Sem build-id (Android 8-), o casamento é por NOME,
# e isso é palpite, não prova:
#
#   - 1 candidato  -> resolve, e CADA LINHA sai marcada
#                     [NAO VERIFICADO: sem BuildId, casado por nome];
#   - 2+ candidatos-> NÃO resolve. Lista os build-ids e diz como escolher
#                     (--build-id). Escolher no chute é como o dev acaba
#                     debugando a função do build errado.
#
# Sem símbolos daquele build-id, a resposta é o que o Android já daria — offset
# + build-id — e diz exatamente qual binário está faltando.
#
# ACHADO DA REVISÃO DE f59e9ff: a versão anterior devolvia só o primeiro
# candidato do INDEX e imprimia a função SEM AVISO. No crash do SA2 isso mostrou,
# com toda a confiança de um resultado verificado, a função de outro build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"

SYMBOLS_DIR="${SYMBOLS_DIR:-$ROOT/out/release/symbols}"
MODE="file"   # "file" (tombstone) ou "offset" (--offset)
TOMBSTONE=""
OFFSET=""
BUILD_ID=""
FRAME_NAME=""

die() { echo "symbolize: $*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
    case "$1" in
        --offset) MODE=offset; shift; OFFSET="${1:-}"; shift ;;
        --symbols) shift; SYMBOLS_DIR="${1:-}"; shift ;;
        --build-id) shift; BUILD_ID="${1:-}"; shift ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) if [ "$MODE" = file ]; then TOMBSTONE="$1"; else FRAME_NAME="${1:-}"; fi; shift ;;
    esac
done

A2L="$(symbols_addr2line)"
[ -n "$A2L" ] || die "llvm-addr2line/addr2line ausente: instale o NDK ou ponha no PATH"

# symbolize_one <rótulo> <build-id> <offset> <nome-binário> [procedência]
# procedência: "buildid" (casou pelo build-id: resultado verificado) ou
# "nome" (casou pelo nome, sem build-id no tombstone: palpite marcado).
symbolize_one() {
    local label="$1" bid="$2" off="$3" name="$4" prov="${5:-buildid}" so out fn loc
    local marca=""
    if [ "$prov" = "nome" ]; then
        marca="  [NAO VERIFICADO: sem BuildId, casado por nome]"
    fi
    if [ -z "$bid" ]; then
        # Sem build-id no tombstone E sem correspondência por nome: NÃO tenta
        # adivinhar. Um offset solto de um .so errado é pior que nada.
        printf '  %-5s %-12s %-16s SEM BUILD-ID no tombstone e nenhum símbolo com este nome\n' \
            "$label" "$off" "$name"
        printf '        (Android 8- não escreve BuildId; guarde os símbolos com tools/build_release.sh)\n'
        return 0
    fi
    if ! so="$(symbols_find "$SYMBOLS_DIR" "$bid" "$name" 2>/dev/null)"; then
        if [ -n "$name" ] && so="$(symbols_find "$SYMBOLS_DIR" "$bid" 2>/dev/null | head -n 1)"; then
            :
        else
            printf '  %-5s %-12s SEM SÍMBOLOS para o build-id %s\n' "$label" "$off" "$bid"
            printf '        o binário que o device rodou não está em %s\n' "$SYMBOLS_DIR"
            printf '        (o build-id muda com o diretório de build se jni/repro.mk faltar)\n'
            return 0
        fi
    fi
    # llvm-addr2line: -f função, -C des-Demangla, -i mostra a cadeia de inlining.
    # O tombstone do Android dá pc = offset no ARQUIVO (o map mostra o .so
    # mapeado com offset 0), então o offset vai direto, sem subtrair load bias.
    out="$("$A2L" -f -C -i -e "$so" "$off" 2>/dev/null || true)"
    if [ -z "$out" ]; then
        printf '  %-5s %-12s addr2line nao resolveu em %s\n' "$label" "$off" "$(basename "$so")"
        return 0
    fi
    # Achata a cadeia de inlining: a última função é a mais externa (a que o
    # offset realmente pertence quando não há inline). Mostra a primeira
    # (mais interna) e a linha, que é o que o usuário precisa para achar no editor.
    fn="$(printf '%s\n' "$out" | sed -n '1p')"
    loc="$(printf '%s\n' "$out" | sed -n '2p')"
    printf '  %-5s %-12s %-16s %s%s\n' "$label" "$off" "$(basename "$so" .so)" "$fn" "$marca"
    printf '        %s\n' "$loc"
}

if [ "$MODE" = offset ]; then
    [ -n "$OFFSET" ] || die "--offset sem o offset (ex: --offset <build-id> 0x1bb34)"
    [ -n "$BUILD_ID" ] || die "--offset sem --build-id"
    printf 'tombstone: build-id %s offset %s\n' "$BUILD_ID" "$OFFSET"
    symbolize_one "#00" "$BUILD_ID" "$OFFSET" "$FRAME_NAME"
    exit 0
fi

[ -n "$TOMBSTONE" ] || die "uso: $0 <tombstone> | --offset <build-id> <off>"
[ -f "$TOMBSTONE" ] || die "tombstone ausente: $TOMBSTONE"
[ -d "$SYMBOLS_DIR" ] || die "sem símbolos em $SYMBOLS_DIR (use --symbols DIR)"

printf 'tombstone: %s\n' "$TOMBSTONE"
printf 'símbolos:  %s\n\n' "$SYMBOLS_DIR"

# Formato do backtrace do Android (Android 9+):
#   #04 pc 000000000001bb34  /data/.../u_patch.so (BuildId: 041d9b51...)
# Formatos mais antigos trazem o nome do símbolo entre parênteses, sem build-id:
#   #04 pc 000000000001bb34  /system/lib64/libfoo.so (func+24)
#
# O AWK agrupa por frame: guarda o offset, o caminho e o build-id do frame, e
 # imprime quando o frame fecha. O nome do binário vem do próprio caminho, que é
 # o que o symbolize_one usa para achar o .so certo dentro de symbols/<build-id>/.
 #
 # Formatos que o AWK tem que aceitar (Android 8-, 9+, e o "sem build-id"):
 #   #04 pc 000000000001bb34  /data/.../u_patch.so (BuildId: 041d9b51...)
 #   #03 pc 0000000001b498a0  /data/.../libil2cpp.so (il2cpp_type_get_name+24) (BuildId: ffd0d6...)
 #   #04 pc 000000000001bb34  /system/lib64/libfoo.so (func+24)
 awk -v symdir="$SYMBOLS_DIR" -v finder="$ROOT/tools/symbols_find_by_name.sh" '
    /^[[:space:]]*#[0-9]+[[:space:]]+pc[[:space:]]/ {
        if (label != "") printf "%s\t%s\t%s\t%s\n", label, off, so, bid
        label = $1; off = $3; so = ""; bid = ""
        for (i = 4; i <= NF; i++) {
            # o caminho do .so: primeiro token que termina em .so
            if (so == "" && $i ~ /\.so$/) so = $i
            # (BuildId: 041d9b51c6f2...) aparece em DUAS formas no campo do
            # tombstone, e as duas occurrem no mundo real:
            #   colado:  (BuildId:041d9b51...)   -> num token só
            #   partido: (BuildId: 041d9b51...)   -> o hash é o token SEGUINTE
            # Pegar só a forma colada faz TODO build-id sair vazio, e aí a
            # busca por nome assume o .so errado (foi exatamente o que
            # aconteceu: frame de libil2cpp reportado como função do u_patch).
            if ($i ~ /BuildId:/) {
                tok = $i
                sub(/^.*BuildId:/, "", tok); gsub(/[()]/, "", tok)
                if (tok == "") tok = $(i + 1)
                gsub(/[()]/, "", tok)
                if (tok != "") bid = tok
            }
        }
        if (bid == "" && so != "") {
            # Tombstone sem BuildId: casamento por NOME. A CONTAGEM importa: com
            # um candidato dá para resolver (marcado como palpite); com dois ou
            # mais, escolher um no chute é pior que não resolver — e o shell
            # precisa saber a contagem para=listar os candidatos e pedir
            # --build-id. Por isso vai "nome:<n>:<bid>" (ou "nome?:" se varios).
            cmd = "SYMDIR=\"" symdir "\" \"" finder "\" \"" so "\""
            n = 0
            while ((cmd | getline line) > 0) { n++; if (n == 1) first = line }
            close(cmd)
            if (n == 1) { sub(/\r?$/, "", first); bid = "nome:1:" first }
            else if (n > 1) bid = "nome?:" n
        }
        next
    }
    END { if (label != "") printf "%s\t%s\t%s\t%s\n", label, off, so, bid }
' "$TOMBSTONE" |
while IFS=$'\t' read -r label off so bid; do
    base="$(basename "$so" 2>/dev/null || printf '%s' "$so")"
    base="${base%.so}"
    case "$bid" in
        nome\?:*)
            # 2+ builds com este nome: NAO resolve. Escolher um aqui é como o
            # dev acaba debugando a função do build errado — que foi
            # exatamente o defeito achado na revisão de f59e9ff.
            n="${bid#nome?:}"
            printf '  %-5s %-12s %-16s AMBIGUO: %s builds guardados com este nome\n' \
                "$label" "$off" "$base" "$n"
            printf '        o tombstone nao traz BuildId (Android 8-), e ha mais de um\n'
            printf '        candidato pelo nome. Escolha o build-id certo e rode de novo:\n'
            printf '          tools/symbolize.sh --offset <build-id> %s   (este frame)\n' "$off"
            printf '        candidatos:\n'
            SYMDIR="$SYMBOLS_DIR" "$ROOT/tools/symbols_find_by_name.sh" "$base" |
                sed 's/^/          /'
            ;;
        nome:1:*)
            symbolize_one "$label" "${bid#nome:1:}" "$off" "$base" nome
            ;;
        *)
            symbolize_one "$label" "$bid" "$off" "$base" buildid
            ;;
    esac
done

