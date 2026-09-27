#!/usr/bin/env bash
# test/symbols/ndk_path_test.sh — o build não pode depender de ONDE o NDK está.
#
# ACHADO DA REVISÃO DE a1783f5: a raiz do NDK era descoberta por um glob em
# "$HOME/Android/Sdk/ndk/*". Funciona NESTA máquina e só nesta: com o NDK em
# /opt/android-ndk, em ANDROID_NDK_HOME, num CI, ou no home de outro usuário, o
# glob não acha e o prefix-map da raiz do NDK SOME — que é exatamente o item 3
# (caminho de máquina vaza e o build-id muda). O mesmo valia para o objcopy, que
# era procurado pelo mesmo glob.
#
# O conserto é $(NDK_ROOT), a variável que o próprio NDK define em
# build/core/init.mk (build-local.mk linha 48, antes do add-application.mk da
# linha 199 que inclui o Android.mk deste projeto). O init.mk ABORTA o build se
# ela faltar, então ela está sempre definida no ponto de uso.
#
# O teste prova o conserto como o fuzzer prova parser: constrói a MESMA árvore em
# dois diretórios temporários, uma com o NDK no $HOME e outra com o NDK apontado
# para um caminho FORA do $HOME (symlink, para não duplicar 1 GB), e exige
# sha256 IDÊNTICO do .so entregue E do símbolo guardado, e nenhum caminho de
# máquina em nenhum dos dois.
#
# Sem isto, "reproduzível" só valia para quem tinha o NDK no lugar certo — que é
# a definição de "funciona na minha máquina".
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
TARGET="${SYMBOLS_NDK_TARGET:-u_patch}"
SO_REL="mods/$TARGET/libs/arm64-v8a/lib$TARGET.so"
BINDIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"

die() { echo "ndk-path: $*" >&2; exit 1; }
check() { if [ "$2" = 0 ]; then printf '  [PASS] %s\n' "$1"; else printf '  [FAIL] %s\n' "$1"; fail=1; fi; }

[ -x "$NDK/ndk-build" ] || die "ndk-build ausente: $NDK/ndk-build"
[ -x "$BINDIR/llvm-strip" ] || die "llvm-strip ausente: $BINDIR/llvm-strip"
[ -x "$BINDIR/llvm-readelf" ] || die "llvm-readelf ausente: $BINDIR/llvm-readelf"
command -v python3 >/dev/null || die "python3 ausente"
command -v sha256sum >/dev/null || die "sha256sum ausente"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
fail=0

# --- um NDK FORA do $HOME ---------------------------------------------------
# Symlink, e nao copia: o NDK tem quase 1 GB e o que esta em teste e o CAMINHO,
# nao o conteudo.
FORA="$WORK/ndk-fora-da-home"
ln -s "$NDK" "$FORA"
[ -x "$FORA/ndk-build" ] || die "symlink do NDK ficou inutil: $FORA"

# A árvore tem que ser montada a partir de UM snapshot só (ver o mesmo cuidado
# no build_id_repro_test: duas chamadas a git status em instants diferentes
# davam duas árvores diferentes e o teste acusava bug onde nao havia).
SNAP="$WORK/snapshot.z"
( cd "$ROOT" && git ls-files -c -o --exclude-standard -z ) >"$SNAP"

stage_tree() {
    local dst="$1" f
    mkdir -p "$dst"
    while IFS= read -r -d '' f; do
        [ -f "$ROOT/$f" ] || continue
        case "$f" in out/*|obj/*|libs/*|build/*) continue ;; esac
        mkdir -p "$dst/$(dirname "$f")"
        cp "$ROOT/$f" "$dst/$f"
    done <"$SNAP"
}

build_com() {
    local tree="$1" ndk="$2" log="$3"
    ( cd "$tree/mods/$TARGET" && "$ndk/ndk-build" -B -j4 ) >"$log" 2>&1
}

# --- 1. as duas árvores ------------------------------------------------------
# A diferença entre os dois builds é de ONDE o NDK parece estar, e há duas
# maneiras de isso estar errado:
#
#   A) o build é INVOCADO por um caminho fora do $HOME (symlink em /tmp). O
#      ndk-build resolve o próprio caminho e usa o caminho REAL, então o build
#      fica igual — e é assim que tem que ser: quem invoca por um symlink não
#      deve pagar por isso.
#
#   B) o HOME do build NÃO tem o NDK. Esta é a regressão que importa, e a
#      primeira versão do teste não pegava: com o symlink, o ndk-build
#      resolvia de volta para $HOME, o glob do $HOME achava o NDK, e a
#      sabotagem (voltar o glob) PASSAVA. Um glob em $HOME é uma suposição sobre
#      onde o NDK está; um HOME sem NDK é essa suposição errada.
echo "ndk-path: mesma árvore, NDK no \$HOME e NDK fora do \$HOME"
A="$WORK/ndk-dentro"; B="$WORK/ndk-fora"
stage_tree "$A"; stage_tree "$B"
# Um HOME de mentira, sem Android/Sdk/ndk dentro. O resto do HOME (cache do
# clang, etc.) é criado pelo próprio build.
FAKE_HOME="$WORK/home-sem-ndk"
mkdir -p "$FAKE_HOME"

build_com() {
    local tree="$1" ndk="$2" log="$3" home="${4:-}"
    if [ -n "$home" ]; then
        ( cd "$tree/mods/$TARGET" && HOME="$home" "$ndk/ndk-build" -B -j4 ) >"$log" 2>&1
    else
        ( cd "$tree/mods/$TARGET" && "$ndk/ndk-build" -B -j4 ) >"$log" 2>&1
    fi
}

build_com "$A" "$NDK" "$WORK/a.log" || { tail -8 "$WORK/a.log" >&2; die "build com o NDK do \$HOME falhou"; }
build_com "$B" "$FORA" "$WORK/b.log" "$FAKE_HOME" ||
    { tail -8 "$WORK/b.log" >&2; die "build com NDK fora do HOME (e HOME sem NDK) falhou"; }
[ -f "$A/$SO_REL" ] || die "artefato ausente em A: $SO_REL"
[ -f "$B/$SO_REL" ] || die "artefato ausente em B: $SO_REL"

# --- 2. sha256 do símbolo (o que vai para symbols/) -------------------------
HA=$(sha256sum "$A/$SO_REL" | cut -d' ' -f1)
HB=$(sha256sum "$B/$SO_REL" | cut -d' ' -f1)
if [ "$HA" = "$HB" ]; then
    printf '  [PASS] símbolo: sha256 igual com o NDK em 2 caminhos (%s)\n' "${HA:0:16}"
else
    printf '  [FAIL] símbolo: sha256 DIVERGE\n        %s  (no HOME)\n        %s  (fora do HOME)\n' "$HA" "$HB"
    fail=1
fi

# --- 3. sha256 do ENTREGUE (o que vai para o device) ------------------------
"$BINDIR/llvm-strip" --strip-unneeded -o "$WORK/entregue-a.so" "$A/$SO_REL"
"$BINDIR/llvm-strip" --strip-unneeded -o "$WORK/entregue-b.so" "$B/$SO_REL"
DA=$(sha256sum "$WORK/entregue-a.so" | cut -d' ' -f1)
DB=$(sha256sum "$WORK/entregue-b.so" | cut -d' ' -f1)
if [ "$DA" = "$DB" ]; then
    printf '  [PASS] entregue: sha256 igual com o NDK em 2 caminhos (%s)\n' "${DA:0:16}"
else
    printf '  [FAIL] entregue: sha256 DIVERGE\n        %s\n        %s\n' "$DA" "$DB"
    fail=1
fi

# --- 4. nenhum caminho de máquina, em nenhum dos dois ----------------------
# O build com o NDK fora do $HOME é o que pega o bug: se a raiz do NDK não for
# mapeada, é o DWARF DELE que vaza, e ele aparece justamente nesse build.
#
# O padrão é ANCORADO no início da string: "/tmp/" solto casaria com
# /data/local/tmp/mods/%s, que é caminho de APARELHO e é legítimo (foi o que a
# primeira versão acusou, num falso positivo).
vazou=0
for alvo in "$A/$SO_REL" "$B/$SO_REL" "$WORK/entregue-a.so" "$WORK/entregue-b.so"; do
    n=$(strings "$alvo" 2>/dev/null | grep -cE '/home/|/Users/|^/tmp/' || true)
    if [ "$n" -gt 0 ]; then
        printf '  [FAIL] %s leva %s caminho(s) de maquina:\n' "$(basename "$alvo")" "$n"
        strings "$alvo" | grep -E '/home/|/Users/|^/tmp/' | sort -u | head -2 | sed 's/^/          /'
        vazou=1
    fi
done
check "nenhum caminho de maquina (nem do NDK fora do \$HOME)" "$vazou"

# --- 5. o .so ainda tem o que o dev precisa --------------------------------
"$BINDIR/llvm-readelf" -S "$B/$SO_REL" >"$WORK/sec-b.txt" 2>/dev/null || true
if grep -q '\.symtab' "$WORK/sec-b.txt" && grep -q '\.debug_info' "$WORK/sec-b.txt"; then
    printf '  [PASS] o .so continua com .symtab e .debug_info (o symbolize.sh funciona)\n'
else
    printf '  [FAIL] o .so perdeu simbolo: sem isso o crash nao vira funcao:linha\n'
    fail=1
fi

# --- 6. a guarda: a raiz do NDK vem do NDK, e nao de um glob no $HOME -----
# "esta definido no ponto de uso" so se prova DENTRO de um ndk-build de
# verdade — em make puro, sem o init.mk do NDK, NDK_ROOT estaria vazio e a
# afirmacao seria falsa (e foi o que a primeira versao deste check mediu).
# O gancho BEPINEX_REPRO_DEBUG=1 e lido de um $(warning) do repro.mk.
# O probe tambem com o HOME sem NDK: e nele que a diferenca entre $(NDK_ROOT) e
# o glob do $HOME aparece.
probe_out=$( cd "$ROOT/mods/$TARGET" && HOME="$FAKE_HOME" \
    BEPINEX_REPRO_DEBUG=1 "$FORA/ndk-build" -B -j4 2>&1 )
# Extração por grep -o, não por sed com âncora: a linha do probe é longa e tem
# varios colchetes, e a âncora "$" do sed engolia o resto.
ndkroot_v="$(printf '%s\n' "$probe_out" | grep -o 'BEPINEX-PROBE NDK_ROOT=\[[^]]*\]' |
    head -1 | sed 's/.*\[//; s/\]//')"
objcopy_v="$(printf '%s\n' "$probe_out" | grep -o 'OBJCOPY=\[[^]]*\]' |
    head -1 | sed 's/.*\[//; s/\]//')"
flags_v="$(printf '%s\n' "$probe_out" | grep -o 'FLAGS=\[.*\]' |
    head -1 | sed 's/^FLAGS=\[//; s/\]$//')"
check "NDK_ROOT nao esta vazio no ponto de uso (dentro do ndk-build)" \
    "$([ -n "$ndkroot_v" ] && echo 0 || echo 1)"
# O NDK resolve o proprio caminho (o symlink vira o caminho real), e o que
# entra no prefix-map e o caminho REAL que o compilador vai ver. Por isso os
# checks comparam contra o NDK_ROOT OBSERVADO, e nao contra o symlink.
check "o NDK_ROOT observado existe de verdade" \
    "$([ -x "$ndkroot_v/ndk-build" ] && echo 0 || echo 1)"
check "o objcopy vem do NDK_ROOT observado" \
    "$(printf '%s' "$objcopy_v" | grep -qF "$ndkroot_v/" && echo 0 || echo 1)"
check "a raiz do NDK observada entra no prefix-map" \
    "$(printf '%s' "$flags_v" | grep -qF -- "-ffile-prefix-map=$ndkroot_v=" && echo 0 || echo 1)"
# O glob do $HOME e a regressao exata. Se ele voltar, o prefix-map da raiz do
# NDK pode sumir e o build-id volta a depender de onde o NDK esta.
check "nenhuma flag depende de \$HOME (o glob do achado)" \
    "$(printf '%s' "$flags_v" | grep -qF 'HOME' && echo 1 || echo 0)"

[ "$fail" -eq 0 ] || die "o build depende de ONDE o NDK esta (ver acima)"
echo "ndk-path: OK (NDK em 2 caminhos, mesmos bytes)"
exit 0
