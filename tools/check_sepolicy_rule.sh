#!/usr/bin/env bash
# Confere module/sepolicy.rule contra a GRAMÁTICA DO magiskpolicy, para o build
# não empacotar um arquivo que o Magisk vai recusar (ou, pior, aceitar em parte:
# statement que não bate vira só um warn e as outras linhas continuam).
#
# Fonte da verdade: magiskpolicy, native/src/sepolicy/statement.rs do Magisk.
#   - statement.rs:310-330  tabela de keywords do tokenizador: allow, deny,
#     auditallow, dontaudit, allowxperm, auditallowxperm, dontauditxperm,
#     permissive, enforce, typeattribute, type, attribute, type_transition,
#     type_change, type_member, genfscon. NÃO existe "self" e nem token de ":".
#   - statement.rs:372-384  gramática por statement (AL/DN/AA/DA = sterm src,
#     sterm tgt, sterm class, sterm perm; TY = ID + attrs).
#   - statement.rs:768-769  help do allow: "allow *source_type *target_type
#     *class *perm_set" (4 campos, perm_set com 1+ entradas).
#   - statement.rs:781-788  formas dos outros: "<action> *type" (permissive/
#     enforce), "typeattribute ^type ^attribute", "type type_name ^(attribute)",
#     "type_transition src tgt class default_type (object_name)",
#     "genfscon fs_name partial_path fs_context".
#   - statement.rs:606-611  linha vazia e linha começada em "#" são puladas.
#   - statement.rs:806-815  grupos com "{...}" (espaço ou vírgula) são expandidos
#     no produto cartesiano, então contam como UM campo.
#   - sepolicy.cpp:244-256  um tipo inexistente (inclusive "self:process" ou
#     "self", que viram string e não existem na tabela) só gera LOGW "target
#     type ... does not exist" e a regra é descartada.
#
# Uso: tools/check_sepolicy_rule.sh [arquivo]  (default: module/sepolicy.rule)
set -eu
RULE=${1:-module/sepolicy.rule}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RULE=$ROOT/${RULE#./}
[ -f "$RULE" ] || { echo "ERRO: $RULE nao existe" >&2; exit 1; }

VERBS='allow deny auditallow dontaudit allowxperm auditallowxperm dontauditxperm permissive enforce typeattribute type attribute type_transition type_change type_member genfscon'
AVVERBS='allow deny auditallow dontaudit'
bad=0
lineno=0

while IFS= read -r raw; do
    lineno=$((lineno + 1))
    line=${raw%%#*}                 # comentário: statement.rs:608
    line=$(printf '%s' "$line" | tr -s ' \t')
    [ -n "$line" ] || continue      # linha vazia: statement.rs:608
    # "{ a b }" e "a,b" contam como um campo (statement.rs:806-815)
    norm=$(printf '%s' "$line" | sed -e 's/{[^}]*}/@grp/g' -e 's/@grp/@grp/g' -e 's/,/ /g' -e 's/  */ /g' -e 's/^ //' -e 's/ $//')
    verb=${norm%% *}
    case " $VERBS " in
        *" $verb "*) ;;
        *) echo "linha $lineno: verbo desconhecido '$verb'" >&2; bad=1; continue ;;
    esac
    # ":" não é token do magiskpolicy: vira parte do nome do tipo e o lookup falha
    if printf '%s' "$norm" | grep -q ':'; then
        echo "linha $lineno: sintaxe de .te/CIL com ':' — use 'allow <src> <tgt> <class> <perm>' ($line)" >&2
        bad=1
        continue
    fi
    # "self" não é palavra-chave em statement.rs:310-330; precisa do tipo real
    for w in $norm; do
        if [ "$w" = self ]; then
            echo "linha $lineno: 'self' nao existe no magiskpolicy — use o tipo do dominio (ex.: appdomain)" >&2
            bad=1
        fi
    done
    # contagem de campos por familia de statement (o verbo ja foi Accounted:
    # $# conta os argumentos DEPOIS dele)
    read -r -a fields <<< "$norm"
    v=${fields[0]}
    n=$((${#fields[@]} - 1))
    if case " $AVVERBS " in *" $v "*) true ;; *) false ;; esac; then
        [ "$n" -ge 4 ] || { echo "linha $lineno: allow precisa de >=4 campos (src tgt class perm): $line" >&2; bad=1; }
    else
        case "$v" in
        type)
            [ "$n" -ge 2 ] || { echo "linha $lineno: type precisa de <nome> <atributo>: $line" >&2; bad=1; }
            ;;
        typeattribute)
            [ "$n" -ge 2 ] || { echo "linha $lineno: typeattribute precisa de <type> <attr>: $line" >&2; bad=1; }
            ;;
        permissive|enforce)
            [ "$n" -eq 1 ] || { echo "linha $lineno: $v precisa de exatamente 1 tipo: $line" >&2; bad=1; }
            ;;
        type_transition)
            [ "$n" -ge 4 ] || { echo "linha $lineno: type_transition precisa de >=4 campos: $line" >&2; bad=1; }
            ;;
        type_member)
            [ "$n" -ge 4 ] || { echo "linha $lineno: type_member precisa de >=4 campos: $line" >&2; bad=1; }
            ;;
        genfscon)
            [ "$n" -ge 3 ] || { echo "linha $lineno: genfscon precisa de >=3 campos: $line" >&2; bad=1; }
            ;;
        *)
            [ "$n" -ge 1 ] || { echo "linha $lineno: $v sem argumentos: $line" >&2; bad=1; }
            ;;
    esac
    fi
done <"$RULE"

if [ "$bad" = 0 ]; then
    echo "sepolicy.rule: $lineno linhas, todas na gramatica do magiskpolicy"
else
    echo "ERRO: $RULE tem statement(s) que o magiskpolicy nao entende" >&2
fi
exit "$bad"
