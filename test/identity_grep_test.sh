#!/usr/bin/env bash
# test/identity_grep_test.sh — T4 (identidade): NENHUM strstr/strcasestr/
# strcmp "contains" pode decidir IDENTIDADE de pacote em jni/.
#
# POR QUE (F1/X1, reprovado pelo kimi): o gate dos verbos BC era
# strstr(pkg, "jp.co.ponos.battlecatsen") — com.evil.jp.co.ponos.battlecatsen
# passava e lia a árvore root-only bc_mods. O grep cru não distingue
# "strstr para path traversal" (LEGÍTIMO, companion.cpp:983) de "strstr para
# identidade de pacote" (PROIBIDO). Este check mantém a LISTA PERMITIDA
# documentada: cada strstr que sobra em jni/ é ou path-traversal, ou nome de
# BIBLIOTECA (cocos2d, frida, libname do jogo) — nunca identidade de pacote.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

fails=0

# 1. strstr em jni/*.h e jni/*.cpp: cada ocorrência precisa estar na lista
#    permitida ou ser recusável por motivo documentado.
while IFS= read -r hit; do
    file=${hit%%:*}
    line=${hit#*:}; lineno=${line%%:*}
    content=$(sed -n "${lineno}p" "$file")
    ok=0
    # permitidos: path-traversal (".."), library names, e o bc_pattern_scan
    case "$content" in
        *"\"..\""*) ok=1 ;;  # path traversal check (bc_mod_pkg_ok/bc_mod_name_ok)
        *cocos2d*) ok=1 ;;   # engine detection (nome de biblioteca)
        *frida*) ok=1 ;;     # preflight de gadget (nome de biblioteca)
        *want_name*|*dlpi_name*|*libname*) ok=1 ;;  # library loader (nome de .so)
        *candidate*) ok=1 ;; # mono_min: procura nome de biblioteca
    esac
    # proibido: strstr com nome de PACOTE (ponto no literal) — mas só em
    # linha de CÓDIGO, não em comentário de documentação (que explica o bug)
    # SC2001: expansão de parâmetro em vez de sed — ${content%%[![:space:]]*}
    # deixa só o whitespace inicial, e o # remove esse prefixo.
    stripped=${content#"${content%%[![:space:]]*}"}
    case "$stripped" in
        "//"*|"#"*|"*"*) ok=1 ;;  # comentário
    esac
    if [ "$ok" = 0 ] && echo "$content" | grep -qE 'strstr.*"[a-z]+\.[a-z]+'; then
        ok=0
    fi
    if [ "$ok" = 0 ]; then
        echo "  [FAIL] strstr decidindo identidade: $hit" >&2
        echo "         linha: $content" >&2
        fails=$((fails + 1))
    fi
done < <(grep -rn "strstr" jni/*.h jni/*.cpp 2>/dev/null || true)

# 2. o matcher de identidade tem que existir e ser IGUALDADE (memcmp/strcmp),
#    não substring: bc_path_is_bc é o ponto único
if ! grep -q "memcmp.*BC_BC_PKG" jni/bc_path_decide.h; then
    echo "  [FAIL] bc_path_is_bc não usa memcmp com BC_BC_PKG (igualdade exata)" >&2
    fails=$((fails + 1))
fi
if grep -q "strstr.*jp\.co\.ponos" jni/ -r 2>/dev/null; then
    echo "  [FAIL] strstr com o nome do BC em jni/ (voltou o F1?)" >&2
    fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
    echo "identity_grep: $fails violação(ões) de identidade em jni/" >&2
    exit 1
fi
echo "identity_grep: nenhuma strstr de identidade em jni/; bc_path_is_bc é memcmp exato"
