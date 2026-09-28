#!/usr/bin/env bash
# test/symbols/build_id_repro_test.sh — o build-id tem que depender do CÓDIGO,
# não do diretório de build.
#
# ACHADO (device POCO C75, SA2, tombstone_07_f4field): o tombstone traz o
# build-id do u_patch.so e nada mais, e o build-id NÃO casa entre dois builds
# do mesmo commit. O linker do NDK calcula o NT_GNU_BUILD_ID sobre os .o de
# entrada, e o caminho do diretório de build está no DWARF deles. Verificado:
# mesmo commit (5ed8019), /tmp/.../repro-a -> 62c539ed..., /tmp/.../repro-b ->
# e38e4fa8...
#
# Consequência prática: um tombstone do usuário não pode ser cruzado com os
# símbolos da build, e um crash leva 12 builds de tentativa e erro para virar
# função:linha (foi o que aconteceu). O conserto é o -ffile-prefix-map de
# jni/repro.mk; este teste é o que impede a flag de sumir em silêncio.
#
# O teste compila o MESMO commit em DOIS diretórios temporários com
# profundidade de caminho diferente (um deles propositalmente fundo, para
# pegar o caso em que o map cobre o caso comum mas não o aninhado) e exige:
#   - os .so byte a byte iguais;
#   - o build-id igual;
#   - o .so com .symtab (senão o build-id não serve para simbolizar nada);
#   - o build-id mudando quando o CÓDIGO muda (senão seria uma constante).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
NDK_BUILD="$NDK/ndk-build"
BINDIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
# Alvos: o LOADER e um mod. O loader tem raiz diferente (jni/ vs mods/<id>/jni/),
# e foi exatamente a diferença de um nível que o include do repro.mk errou
# durante o desenvolvimento: o build-id dele saía errado sem nenhum aviso, e
# o único sintoma era o verify_all inteiro dar FAIL no primeiro passo.
TARGETS_ALL=(loader u_patch)

die() { echo "build-id-repro: $*" >&2; exit 1; }

[ -x "$NDK_BUILD" ] || die "ndk-build ausente: $NDK_BUILD"
[ -x "$BINDIR/llvm-readelf" ] || die "llvm-readelf ausente: $BINDIR/llvm-readelf"
command -v git >/dev/null || die "git ausente"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Dois diretórios de profundidades diferentes: A propositalmente fundo, porque
# o erro que se quer pegar é "o map cobre $PWD mas não o pai do repo".
A="$WORK/aa/bb/cc/dd"
B="$WORK/zz"
mkdir -p "$A" "$B"

# A lista de arquivos é tirada UMA VEZ e as duas árvores são montadas a partir
# dela. Duas chamadas separadas a git status seriam uma corrida: se a worktree
# mudasse entre as duas (o gate buscou o shellcheck, um build terminou, um
# arquivo apareceu), as árvores A e B teriam conteúdo diferente e o teste
# acusaria "build-id não reproduzível" sendo que o build está correto. Foi
# exatamente o que aconteceu nas primeiras execuções: falhava 1 em 3.
# A lista vai para um ARQUIVO, e não para uma variável: bash descarta byte NUL
# de variável, e a lista precisa do terminador NUL para não quebrar em nome de
# arquivo com espaço.
# `git ls-files -c -o --exclude-standard` é UM comando: versionado + não
# versionado, menos o ignorado. Duas chamadas separadas (ls-files e status)
# davam uma janela em que a worktree podia mudar entre elas, e as duas árvores
# saíam com conteúdo diferente — o teste acusava "build-id não reproduzível"
# com o build absolutamente correto. Foi o que vi falhar 1 em 8.
SNAP="$WORK/snapshot.z"
( cd "$ROOT" && git ls-files -c -o --exclude-standard -z ) >"$SNAP"

stage_tree() {
    local dst="$1" f
    while IFS= read -r -d '' f; do
        [ -f "$ROOT/$f" ] || continue
        # Só diretórios de artefato. NÃO filtrar por extensão: jni/lib/arm64-v8a/
        # libdobby.a é .a e é VERSIONADO (dependência do build), e sem ele o
        # ndk-build aborta com "LOCAL_SRC_FILES points to a missing file".
        # O resto (out/, obj/, libs/) já não entra: é gitignored, então nem
        # git ls-files nem git status -uall listam.
        case "$f" in out/*|obj/*|libs/*|build/*) continue ;; esac
        mkdir -p "$dst/$(dirname "$f")"
        cp "$ROOT/$f" "$dst/$f"
    done <"$SNAP"
}

stage_tree "$A"
stage_tree "$B"

# build_one <arvore> <alvo> <log>
# O cwd é o diretório do PROJETO (mods/<id>/ pro mod, a raiz pro loader) — é de
# lá que o ndk-build é chamado no fluxo real, e o build-id tem de não depender
# disso (ver jni/repro.mk).
build_one() {
    local tree="$1" target="$2" log="$3" dir
    case "$target" in
        loader)  dir="$tree" ;;
        u_patch) dir="$tree/mods/u_patch" ;;
        *) die "alvo desconhecido: $target" ;;
    esac
    ( cd "$dir" && "$NDK_BUILD" -B -j4 ) >"$log" 2>&1
}

build_id() { "$BINDIR/llvm-readelf" -n "$1" 2>/dev/null | awk '/Build ID:/ {print $3; exit}'; }


# so_rel <alvo>: caminho do .so dentro da arvore
so_rel() {
    case "$1" in
        loader)   echo "libs/arm64-v8a/libbc-poc.so" ;;
        u_patch)  echo "mods/u_patch/libs/arm64-v8a/libu_patch.so" ;;
        *) die "alvo desconhecido: $1" ;;
    esac
}

fail=0

# check_target <alvo>
check_target() {
    local target="$1" rel ida idb
    rel="$(so_rel "$target")"
    echo "build-id-repro: $target em dois diretórios de profundidades diferentes"
    build_one "$A" "$target" "$WORK/a-$target.log" ||
        { cat "$WORK/a-$target.log" >&2; die "build de $target falhou em $A"; }
    build_one "$B" "$target" "$WORK/b-$target.log" ||
        { cat "$WORK/b-$target.log" >&2; die "build de $target falhou em $B"; }
    [ -f "$A/$rel" ] || die "artefato ausente em A: $rel"
    [ -f "$B/$rel" ] || die "artefato ausente em B: $rel"
    ida=$(build_id "$A/$rel"); idb=$(build_id "$B/$rel")
    [ -n "$ida" ] || die "sem NT_GNU_BUILD_ID em A/$rel"

    # 1. build-id igual
    if [ "$ida" = "$idb" ]; then
        printf '  [%s] %-12s build-id igual nos 2 diretorios: %s\n' PASS "$target" "$ida"
    else
        printf '  [%s] %-12s build-id DIVERGE: %s vs %s\n' FAIL "$target" "$ida" "$idb"
        fail=1
    fi
    # 2. .so byte a byte igual
    if cmp -s "$A/$rel" "$B/$rel"; then
        printf '  [%s] %-12s .so byte a byte igual entre os 2 diretorios\n' PASS "$target"
    else
        printf '  [%s] %-12s .so DIVERGE: %s byte(s) diferente(s)\n' FAIL "$target" \
            "$(cmp -l "$A/$rel" "$B/$rel" 2>/dev/null | wc -l)"
        fail=1
    fi
    # 3. símbolos presentes (sem isto o build-id não serve para nada)
    #
    # A saída do readelf vai para um ARQUIVO antes do grep. `readelf | grep -q` é
    # instável: o grep -q sai assim que acha, o readelf leva SIGPIPE no meio da
    # escrita e às vezes morre antes de imprimir a seção — dando "sem .symtab"
    # num .so que tem. Foi 1 em 20 execuções, e o detalhe (build-id e bytes
    # IGUAIS, só o check do symtab falhando) é o que denunciou.
    "$BINDIR/llvm-readelf" -S "$A/$rel" >"$WORK/sections-$target.txt" 2>/dev/null || true
    if grep -q '\.symtab' "$WORK/sections-$target.txt"; then
        printf '  [%s] %-12s .so tem .symtab (o release da para simbolizar)\n' PASS "$target"
    else
        printf '  [%s] %-12s .so saiu SEM .symtab: o build voltou a stripar\n' FAIL "$target"
        fail=1
    fi
    # 4. o binário que vai pro device tem que continuar stripped
    if [ -n "${SYMBOLS_DIR:-}" ] && [ -f "$SYMBOLS_DIR/$ida/$target.so" ]; then
        "$BINDIR/llvm-readelf" -S "$SYMBOLS_DIR/$ida/$target.so" >"$WORK/sym-$target.txt" 2>/dev/null || true
        if grep -q '\.symtab' "$WORK/sym-$target.txt"; then
            printf '  [%s] %-12s simbolos guardados com .symtab\n' PASS "$target"
        else
            printf '  [%s] %-12s simbolos guardados SEM .symtab\n' FAIL "$target"
            fail=1
        fi
    fi
}

for t in "${TARGETS_ALL[@]}"; do check_target "$t"; done

# 5. o build-id tem que reagir ao CÓDIGO. Um build-id constante passaria nos
#    testes 1-3 e seria inútil: ele precisa mudar quando o fonte muda.
C="$WORK/cc"
mkdir -p "$C"
stage_tree "$C"
# Uma alteração que muda o CÓDIGO GERADO. Um comentário não serviria: o
# compilador não o materializa, o .so sairia idêntico e o teste passaria por
# engano (build reprodutível de verdade ignora comentário). Esta função emite
# código de verdade, então o .text muda e o build-id TEM de mudar.
touch "$C/mods/u_patch/jni/u_patch_mod.cpp"
printf '\nextern "C" __attribute__((used)) int build_id_repro_marker(void) { return 0x5a5a; }\n' \
    >>"$C/mods/u_patch/jni/u_patch_mod.cpp"
build_one "$C" u_patch "$WORK/c.log" || { cat "$WORK/c.log" >&2; die "build falhou em C"; }
BIDC=$(build_id "$C/mods/u_patch/libs/arm64-v8a/libu_patch.so")
BIDA=$(build_id "$A/mods/u_patch/libs/arm64-v8a/libu_patch.so")
if [ "$BIDC" != "$BIDA" ]; then
    printf '  [%s] build-id muda quando o codigo muda (%s)\n' PASS "$(printf '%.8s' "$BIDC")"
else
    printf '  [%s] build-id NAO mudou com o codigo: e uma constante, nao identifica build\n' FAIL
    fail=1
fi

# 6. a guarda existe e está no caminho do build de TODO projeto que vira .so.
#    Verificar o repro.mk sozinho passaria mesmo com o include removido, e foi
#    assim que o include errado do loader passou: repro.mk existia, o include
#    é que apontava um nível acima, e nada reclamava.
bad_mk=0
while IFS= read -r app; do
    if ! grep -q 'include .*jni/repro.mk' "$app" 2>/dev/null; then
        echo "        $app nao inclui jni/repro.mk"
        bad_mk=1
    fi
done < <(find "$A" -name Application.mk -not -path '*/out/*')
# `-f` antes de `prefix-map`: a palavra aparece em comentário, e um grep pela
# palavra passava com a flag removida — foi o que a sabotagem expôs.
if grep -qE '^\s*-f(file|debug)-prefix-map=' "$A/jni/repro.mk" 2>/dev/null &&
   grep -qE '^APP_STRIP_MODE\s*:=' "$A/jni/repro.mk" 2>/dev/null &&
   [ "$bad_mk" -eq 0 ]; then
    printf '  [%s] todo Application.mk inclui jni/repro.mk (prefix-map + APP_STRIP_MODE)\n' PASS
else
    printf '  [%s] algum Application.mk nao inclui jni/repro.mk, ou ele perdeu prefix-map/APP_STRIP_MODE\n' FAIL
    fail=1
fi

[ "$fail" -eq 0 ] || die "build-id/ simbolos NAO conferem entre diretorios (ver acima)"
echo "build-id-repro: OK (loader e u_patch reproduziveis)"
exit 0
