#!/usr/bin/env bash
# test/old_mods_path_check.sh — nenhum caminho velho de árvore de mods
# (/data/local/tmp/mods) fora das exceções TIPADAS.
#
# POR QUE ESTE CHECK EXISTE (P2 da pré-revisão): a árvore de mods mudou para
# /data/adb/bepinex/mods (root-only). Cada menção perdida ao caminho velho é
# ou uma instrução que não funciona mais, ou um texto que mente sobre onde o
# mod mora. O grep cru achava uma por commit de revisão — agora é o gate que
# acha.
#
# POR QUE AS EXCEÇÕES AGORA EXIGEM TIPO (S3 da revisão do kilo em
# companion-followups-4): a lista anterior era AUTO-CERTIFICADA — nada
# verificava a justificativa. O kilo adicionou jni/companion.cpp (depois
# README.md) com o caminho velho plantado, e o check saiu 0 imprimindo
# "nenhuma menção fora das exceções justificadas". A partir daqui cada
# exceção declara o TIPO de ocorrência, e o check CONFERENCE o tipo contra
# o que o arquivo e a LINHA realmente são:
#
#   doc        registro histórico/roteiro .md da lista FIXA HISTORICOS.
#              Um .md fora da lista (README.md) com o caminho velho NÃO
#              passa: doc é status declarado, não extensão de arquivo.
#   migrador   os scripts que NASCERAM para citar a árvore velha (lista
#              FIXA MIGRADORES): o uninstall limpa o que ficou no lugar
#              antigo, o migrador sai de lá. Fora desses dois, código
#              executável com caminho velho não tem tipo que salve.
#   comentario TODA ocorrência no arquivo é LINHA de comentário (//, /*,
#              *, #, <!--) — o check olha a LINHA, não o rótulo. Código
#              executável com o caminho velho NÃO passa como "comentario".
#   teste      arquivo que mora sob test/ ou manager/test/: o caminho é
#              DADO (corpus de fuzz, fixture de tombstone, string de ataque
#              de injeção, código do sim que roda o kit real). O gate
#              valida a natureza do arquivo; o conteúdo é revisão, e a
#              exceção sai impressa a cada execução do gate.
#   detector   a linha é um grep que PROCURA o caminho velho para ALERTAR
#              (monitor de log histórico) — o check exige "grep" na linha.
#
# Exceção sem tipo, tipo desconhecido, arquivo sem ocorrência NENHUMA
# (exceção morta é mentira, igual a do kilo) ou tipo que não bate com o
# arquivo/linha: FALHA. A contagem no fim é SEMPRE derivada do array.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

# Listas FIXAS: o tipo doc/migrador vale só para estes arquivos, e a
# pertinência é declarada AQUI, nominalmente — não é "qualquer .md".
HISTORICOS=(
    "CHANGELOG.md"
    "docs/DEVICE-ROUND.md"
    "docs/DEVICE-ROUND-2.md"
    "docs/DEVICE-ROUND-3.md"
)
MIGRADORES=(
    "module/uninstall.sh"
    "module/migrate-mods-tree.sh"
)

# Exceções: "arquivo|tipo". Nada entra sem tipo, e o tipo é conferido.
EXCEPTIONS=(
    "module/uninstall.sh|migrador"
    "module/migrate-mods-tree.sh|migrador"
    "CHANGELOG.md|doc"
    "docs/DEVICE-ROUND.md|doc"
    "docs/DEVICE-ROUND-2.md|doc"
    "docs/DEVICE-ROUND-3.md|doc"
    "manager/test/io/github/rianprei/bepinex/manager/test/RootInjectionTableTest.java|teste"
    "test/device/restore-sim.sh|teste"
    "test/fuzz/corpus/frida_config/cfg_00|teste"
    "test/fuzz/corpus/frida_config/cfg_01|teste"
    "test/fuzz/seed_corpus.py|teste"
    "test/symbols/ndk_path_test.sh|comentario"
    "test/symbols/symbolize_injection_test.sh|teste"
    "test/symbols/symbolize_test.sh|teste"
    "tools/device_round2_soak.sh|detector"
)

# A linha é comentário? (// /* * # <!-- no início, após o whitespace)
linha_eh_comentario() {
    local s
    s=${1#"${1%%[![:space:]]*}"}  # strip leading whitespace (SC2001: sem sed)
    case "$s" in
        '//'*|'/*'*|'*'*|'#'*|'<!--'*) return 0 ;;
        *) return 1 ;;
    esac
}

# O arquivo está numa lista fixa? (sim bash puro; as listas são pequenas)
na_lista() {
    local want="$1" item
    shift
    for item in "$@"; do
        [ "$want" = "$item" ] && return 0
    done
    return 1
}

fails=0

# ---- 1. toda ocorrência do caminho velho tem exceção de tipo que BATE ------
while IFS= read -r hit; do
    file=${hit%%:*}
    resto=${hit#*:}
    content=${resto#*:}
    entry=""
    for ex in "${EXCEPTIONS[@]}"; do
        [ "${ex%%|*}" = "$file" ] && { entry="$ex"; break; }
    done
    if [ -z "$entry" ]; then
        echo "  [FAIL] caminho velho fora das exceções: $hit" >&2
        fails=$((fails + 1))
        continue
    fi
    tipo=${entry#*|}
    ok=0
    case "$tipo" in
        doc)
            if na_lista "$file" "${HISTORICOS[@]}"; then
                ok=1
            else
                echo "  [FAIL] tipo 'doc', mas '$file' não é registro histórico da lista fixa: $hit" >&2
            fi
            ;;
        migrador)
            if na_lista "$file" "${MIGRADORES[@]}"; then
                ok=1
            else
                echo "  [FAIL] tipo 'migrador', mas '$file' não é migrador da lista fixa: $hit" >&2
            fi
            ;;
        comentario)
            if linha_eh_comentario "$content"; then
                ok=1
            else
                echo "  [FAIL] tipo 'comentario', mas a linha NÃO é comentário (código executável): $hit" >&2
            fi
            ;;
        teste)
            case "$file" in
                test/*|manager/test/*) ok=1 ;;
                *) echo "  [FAIL] tipo 'teste', mas '$file' não mora em test/: $hit" >&2 ;;
            esac
            ;;
        detector)
            case "$content" in
                *grep*) ok=1 ;;
                *) echo "  [FAIL] tipo 'detector', mas a linha não tem grep: $hit" >&2 ;;
            esac
            ;;
        *)
            echo "  [FAIL] tipo desconhecido '$tipo' na exceção de '$file': $hit" >&2
            ;;
    esac
    [ "$ok" = 0 ] && fails=$((fails + 1))
done < <(git grep -n "data/local/tmp/mods" -- . ':!test/old_mods_path_check.sh' || true)

# ---- 2. toda exceção tem ocorrência REAL (exceção morta é mentira) --------
for ex in "${EXCEPTIONS[@]}"; do
    f=${ex%%|*}
    if ! git grep -q "data/local/tmp/mods" -- "$f" 2>/dev/null; then
        echo "  [FAIL] exceção sem ocorrência real no arquivo: $ex" >&2
        fails=$((fails + 1))
    fi
done

if [ "$fails" -ne 0 ]; then
    echo "old_mods_path: $fails problema(s) — migre o texto para /data/adb/bepinex/mods ou declare a exceção com TIPO verdadeiro (doc|migrador|comentario|teste|detector)" >&2
    exit 1
fi
echo "old_mods_path: nenhuma menção a /data/local/tmp/mods fora das ${#EXCEPTIONS[@]} exceções tipadas e verificadas (${#HISTORICOS[@]} doc, ${#MIGRADORES[@]} migrador; contagem derivada, nunca fixa)"
