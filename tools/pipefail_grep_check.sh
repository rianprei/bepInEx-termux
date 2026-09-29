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
# Por que a here-string é segura: ela entrega o conteúdo ao grep ANTES de ele
# rodar, então o grep passa a ser o único processo do pipeline e não existe
# produtor a matar. A semântica da comparação é idêntica.
#
# DETECÇÃO DE PIPEFAIL
#
# A primeira versão casava só duas formas (`set -euo pipefail` e `set -o
# pipefail`), e a classed como "não usa pipefail" qualquer `set -uo pipefail` —
# que é a forma mais comum em script pequeno. Resultado: 15 scripts com pipefail
# NUNCA eram varridos e a linha de sucesso era FALSA, porque ela anunciava uma
# limpeza que não tinha verificado nada. Agora o detector aceita QUALQUER
# combinação de flags na forma `set ... -o pipefail`, e também `shopt -so pipefail`,
# e o resumo em stdout informa QUANTOS scripts com pipefail existem e QUANTOS
# foram varridos — para o sucesso ser conferível de fora, e não uma promessa.
# shellcheck disable=SC2016  # crase literal em padrao de grep e em printf:
# e o que queremos, nao ha expressao para expandir.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
EXCEPTION_FILE="$ROOT/tools/pipefail_grep.exceptions"

# `set` seguido de qualquer combinação de flags, com pipefail em qualquer
# posição. O detalhe que faz `-euo` casar: essa é UMA flag só, e o `o` é a
# ÚLTIMA letra dela — `set -o pipefail` (o solto) casa, `set -euo pipefail`
# NÃO casaria com um padrão que exigisse `-o` como flag inteira, que foi
# exatamente o bug: 32 scripts com `set -euo pipefail` e 17 com `set -uo` eram
# lidos como "sem pipefail" e nunca varridos.
SET_PIPEFAIL_RE='(^|[;&|[:space:]])set[[:space:]]+(-[a-zA-Z]+[[:space:]]+)*-[a-zA-Z]*o[[:space:]]+pipefail([[:space:]]|$)'
# shopt -so pipefail: mesma opção, sintaxe do shopt.
SHOPT_PIPEFAIL_RE='(^|[;&|[:space:]])shopt[[:space:]]+(-[a-zA-Z]+[[:space:]]+)*-[a-zA-Z]*o[[:space:]]+pipefail([[:space:]]|$)'

erros=()
varridos=0        # scripts que usam pipefail e foram checados de fato
com_pipefail=0    # scripts que usam pipefail no total
total_sh=0        # todos os .sh do repo

usa_pipefail() {
    grep -qE "$SET_PIPEFAIL_RE" "$1" || grep -qE "$SHOPT_PIPEFAIL_RE" "$1"
}

eh_excecao() {
    [ -f "$EXCEPTION_FILE" ] || return 1
    awk -v f="$1" -v n="$2" '
        /^[[:space:]]*#/ { next }
        NF {
            split($0, a, " ")
            if (a[1] != f) next
            # "*" cobre o arquivo inteiro, porque excecao por numero de LINHA e
            # fragil: qualquer edicao acima troca a linha e a excecao deixa de
            # valer, sem ninguem mexer na politica.
            if (a[2] == "*" || a[2] == n) { found=1; exit }
        }
        END { exit(found ? 0 : 1) }' "$EXCEPTION_FILE"
}

while IFS= read -r arquivo; do
    total_sh=$((total_sh + 1))
    usa_pipefail "$arquivo" || continue
    com_pipefail=$((com_pipefail + 1))
    varridos=$((varridos + 1))
    rel=${arquivo#"$ROOT"/}
    n=0
    while IFS=: read -r linha resto; do
        # Comentario nao e codigo. Este repo documenta a propria corrida em
        # comentario, e um lint que acusa o texto que explica o defeito obriga o
        # mantenedor a apagar a explicacao.
        antes=${resto%%#*}
        limpo=$(printf '%s' "$antes" | tr -d '[:space:]')
        [ -z "$limpo" ] && continue          # linha INTEIRA de comentario
        case "$antes" in
            *\"*|*\'*) ;;                     # '#' dentro de aspas e conteudo
        esac
        n=$((n + 1))
        if eh_excecao "$rel" "$linha"; then
            continue
        fi
        # UM erro por sitio. A versao anterior empilhava a mensagem em 6 chunks
        # no array, e `printf '%s\n' "${erros[@]}"` imprimia 6 linhas por sitio e
        # `${#erros[@]}` contava 6: um unico sitio saia como "6 sitio(s)".
        # A mensagem e longa por necessidade, mas cabe em uma unica string.
        erros+=("$(printf '%s:%s: "... | grep -q" em script com pipefail. O grep -q sai cedo, o produtor leva SIGPIPE (141) e o pipefail transforma isso em falha mesmo com o elemento presente. Use here-string (grep -q PADRAO <<<"\$var"), ou grep sem -q redirecionando, ou o valor numa variavel. Se aqui nao da, declare em tools/pipefail_grep.exceptions com o motivo.' "$rel" "$linha")")
    done < <(grep -nE '\|[[:space:]]*(sudo[[:space:]]+)?grep[[:space:]]+-[a-zA-Z]*q' "$arquivo" || true)
done < <(find "$ROOT" -name '*.sh' -not -path "*/.git/*" -not -name 'pipefail_grep_check.sh' | sort)

if [ "${#erros[@]}" -gt 0 ]; then
    for e in "${erros[@]}"; do printf '%s\n' "$e" >&2; done
    printf 'pipefail-grep: %d sitio(s) com "| grep -q" em script com pipefail\n' "${#erros[@]}" >&2
    printf 'pipefail-grep: varredura %d/%d scripts com pipefail (de %d .sh no repo)\n' \
        "$varridos" "$com_pipefail" "$total_sh" >&2
    exit 1
fi
# A linha de sucesso nao e promessa: ela informa o que foi realmente varrido,
# e se `varridos != com_pipefail` isso e um defeito do proprio detector.
if [ "$varridos" -ne "$com_pipefail" ]; then
    printf 'pipefail-grep: DETECTOR inconsistente: %d scripts com pipefail, %d varridos\n' \
        "$com_pipefail" "$varridos" >&2
    exit 1
fi
printf 'pipefail-grep: nenhum "| grep -q" em script com pipefail — varridos %d/%d scripts com pipefail (de %d .sh no repo)\n' \
    "$varridos" "$com_pipefail" "$total_sh"
