#!/usr/bin/env bash
# tools/pipefail_grep_check.sh — nenhum "| grep -q" em script com pipefail.
#
# POR QUE ISSO É FALHA E NÃO ESTILO
#
#   set -o pipefail
#   algo | grep -q PADRAO
#
# O grep -q sai assim que acha o padrão e fecha a leitura do pipe. Se o produtor
# ainda tem bytes para escrever, ele leva SIGPIPE e morre com 141. Com pipefail,
# o pipeline devolve o ÚLTIMO status não-zero — o 141 do produtor — mesmo com o
# elemento PRESENTE. O `if` cai no ramo errado. É uma loteria: só aparece quando
# a saída do produtor passa do buffer do pipe (64 KiB) e o padrão está no COMEÇO.
#
#   forma com corrida : printf '%s' "$sa" | grep -q 'ACHEI'
#   forma sem corrida : grep -q 'ACHEI' <<<"$sa"      # um processo só, ninguém leva SIGPIPE
#
# Por que a here-string e segura: ela cria o arquivo/pipe e escreve o conteúdo
# ANTES de o grep rodar. O grep passa a ser o único processo do pipeline, então
# não existe produtor a matar. A semântica da comparação é idêntica.
#
# A EXCEÇÃO
#
# Um script que NÃO usa `set -o pipefail` não tem o defeito: o status do
# pipeline é o do grep, e o 141 do produtor é descartado. Esses ficam na
# exceção com o motivo obrigatório, e o motivo sai impresso a cada execução.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
EXCEPTION_FILE="$ROOT/tools/pipefail_grep.exceptions"

erros=()
# um arquivo só é forbidido se ELE usa pipefail: é o que dá sentido ao 141
usa_pipefail() {
    grep -qE '^[[:space:]]*set[[:space:]]+-[a-zA-Z]*e[a-zA-Z]*o[[:space:]]*pipefail|set[[:space:]]+-o[[:space:]]*pipefail' "$1"
}

eh_excecao() {
    [ -f "$EXCEPTION_FILE" ] || return 1
    awk -v f="$1" -v n="$2" '
        /^[[:space:]]*#/ { next }
        NF {
            split($0, a, " ")
            if (a[1] != f) next
            # "*" cobre o arquivo inteiro. Existe porque a excecao por numero de
            # LINHA e fragil: qualquer edicao acima do sitio troca a linha e a
            # excecao deixa valer, ou deixa de valer, sem ninguem mexer na
            # politica. Para um arquivo que e a demonstracao do defeito, o
            # certo e o arquivo inteiro, com o motivo escrito.
            if (a[2] == "*" || a[2] == n) { found=1; exit }
        }
        END { exit(found ? 0 : 1) }' "$EXCEPTION_FILE"
}

total_suspeitos=0
total_excecao=0
while IFS= read -r arquivo; do
    rel=${arquivo#"$ROOT"/}
    usa_pipefail "$arquivo" || continue
    n=0
    while IFS=: read -r linha resto; do
        # Comentario nao e codigo. Este repo documenta a propria corrida em
        # comentario (tools/symbols.sh:137, test/fuzz/run_fuzz_gate.sh:76), e
        # um lint que acusa o texto que EXPLICA o defeito obriga o mantenedor
        # a apagar a explicacao ou a silenciar o lint.
        # O padrao e literal de verdade, nao substring de comentario: um '#' no
        # MEIO da linha so conta se o que vier antes for codigo, e nao uma
        # aspas.
        antes=${resto%%#*}
        limpo=$(printf '%s' "$antes" | tr -d '[:space:]')
        if [ -z "$limpo" ]; then
            continue                      # linha INTEIRA de comentario
        fi
        case "$antes" in
            *\"*|*\'*) ;;                 # '#' dentro de aspas e conteudo
        esac
        n=$((n + 1))
        total_suspeitos=$((total_suspeitos + 1))
        if eh_excecao "$rel" "$linha"; then
            total_excecao=$((total_excecao + 1))
            continue
        fi
        erros+=("$rel:$linha: \"... | grep -q\" em script com pipefail. "
               "O grep -q sai cedo, o produtor leva SIGPIPE (141) e o pipefail "
               "transforma isso em falha mesmo com o elemento presente. Use "
               "here-string (grep -q PADRAO <<<\"\$var\"), ou grep sem -q "
               "redirecionando, ou o valor numa variavel. Se aqui nao da, "
               "declare em tools/pipefail_grep.exceptions com o motivo.")
    done < <(grep -nE '\|[[:space:]]*(sudo[[:space:]]+)?grep[[:space:]]+-[a-zA-Z]*q' "$arquivo" || true)
done < <(find "$ROOT" -name '*.sh' -not -path "*/.git/*" -not -name 'pipefail_grep_check.sh' | sort)

if [ "${#erros[@]}" -gt 0 ]; then
    printf '%s\n' "${erros[@]}" >&2
    printf 'pipefail-grep: %d sitio(s) com "| grep -q" em script com pipefail\n' "${#erros[@]}" >&2
    exit 1
fi
printf 'pipefail-grep: nenhum "| grep -q" em script com pipefail (%d suspeito(s), %d por excecao declarada)\n' \
    "$total_suspeitos" "$total_excecao"
