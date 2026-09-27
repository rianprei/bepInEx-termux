#!/usr/bin/env bash
# test/symbols/ship_stripped_test.sh — NADA que sai da máquina carrega símbolo.
#
# ACHADO (revisão do a342185): com `APP_STRIP_MODE := none` em jni/repro.mk todo
# .so de build passou a sair NÃO-stripado, e o preço não era só o diretório de
# build — qualquer consumidor de mods/*/libs/arm64-v8a/*.so começou a LEADER o
# binário gigante:
#
#   tools/pack_bmod.sh   -> mod.so dentro do .bmod (o que o usuário baixa e
#                           compartilha)
#   tools/deploy_mod.sh  -> adb push (o .so que vai pro aparelho)
#   manager/build.sh     -> assets do APK (o APK cresce ~1,6 MB por .so)
#
# O .so não-stripado tem ~1,8 MB contra ~280 KB stripped: 6,7x. A.symbolização
# de crash é o que justificou o não-strip; o binário que o celular recebe nunca
# precisou dele.
#
# Este teste pega a classe inteira: para cada artefato que sai (o .bmod gerado,
# os assets do APK, o stage do deploy), confere que o .so lá dentro NÃO tem
# .symtab, e que o build-id é o mesmo do .so de build (é o build-id que liga o
# tombstone do aparelho aos símbolos guardados).
#
# O CI não tem nem adb nem keystore, então os caminhos de device/APK são
# verificados pelo MESMO helper (symbols_ship) que o build real usa — se um
# consumidor voltar a copiar o .so cru, este teste vê.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
BINDIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
# O mod de teste é criado a partir de mods/_template pelo próprio
# tools/new_mod.sh — é o caminho real de quem cria um mod, e evita o teste
# depender de um mod específico existir no repo (hoje só _template tem
# manifest.json, e o id dele é o placeholder __MOD_ID__).
TARGET="${SYMBOLS_SHIP_TARGET:-shipchk}"
WORK=$(mktemp -d)
# O mod nasce DENTRO do repo (new_mod.sh escreve em mods/<id>/) e sai junto com
# o trap: um gate que deixa mods/shipchk para trás polui `git status` e
# quebra o build-id-repro, que tira o snapshot da arvore.
# O trap direto, e nao `trap cleanup EXIT`: o shellcheck marca SC2329 numa
# funcao que so e referenciada pelo trap, e o gate trata finding como FAIL.
trap 'rm -rf "$WORK" "$ROOT/mods/$TARGET"' EXIT
fail=0

# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"

die() { echo "ship-stripped: $*" >&2; exit 1; }
check() { if [ "$2" = 0 ]; then printf '  [PASS] %s\n' "$1"; else printf '  [FAIL] %s\n' "$1"; fail=1; fi; }

[ -x "$BINDIR/llvm-readelf" ] || die "llvm-readelf ausente: $BINDIR/llvm-readelf"
[ -x "$NDK/ndk-build" ] || die "ndk-build ausente: $NDK/ndk-build"
command -v python3 >/dev/null || die "python3 ausente"

echo "ship-stripped: alvo mods/$TARGET"
[ ! -e "$ROOT/mods/$TARGET" ] || die "mods/$TARGET ja existe; limpe antes"
( cd "$ROOT" && tools/new_mod.sh "$TARGET" ) >"$WORK/newmod.log" 2>&1 ||
    { cat "$WORK/newmod.log" >&2; die "new_mod.sh falhou"; }
[ -f "$ROOT/mods/$TARGET/manifest.json" ] || die "new_mod.sh nao gerou manifest.json"

# --- 0. o .so de build precisa mesmo estar NÃO-stripado ---------------------
# Se o build já saísse stripped, este teste passaria sem provar nada: o símbolo
# nunca existiu para ser vazado.
( cd "$ROOT/mods/$TARGET" && "$NDK/ndk-build" -B -j4 ) >"$WORK/build.log" 2>&1 ||
    { tail -20 "$WORK/build.log" >&2; die "build de mods/$TARGET falhou"; }
SRC="$ROOT/mods/$TARGET/libs/arm64-v8a/lib$TARGET.so"
[ -f "$SRC" ] || die "artefato ausente: $SRC"
"$BINDIR/llvm-readelf" -S "$SRC" >"$WORK/src.sec" 2>/dev/null || true
if grep -q '\.symtab' "$WORK/src.sec"; then
    printf '  [PASS] o .so de build tem .symtab (e 6,7x maior: ha simbolo a vazar)\n'
else
    die "o .so de build saiu STRIPPED: APP_STRIP_MODE := none sumiu de jni/repro.mk e este teste nao prova nada"
fi
SRC_SIZE=$(stat -c%s "$SRC")
SRC_BID=$(symbols_build_id "$SRC")

# --- 1. .bmod: o arquivo que o usuario baixa e compartilha ------------------
echo "ship-stripped: (1) .bmod (pack_bmod.sh)"
[ -f "$ROOT/mods/$TARGET/manifest.json" ] || die "mods/$TARGET/manifest.json ausente"
( cd "$ROOT" && tools/pack_bmod.sh "$TARGET" ) >"$WORK/pack.log" 2>&1 ||
    { cat "$WORK/pack.log" >&2; die "pack_bmod.sh falhou"; }
cat "$WORK/pack.log" | tail -1
BMOD=$(tail -1 "$WORK/pack.log")
[ -f "$BMOD" ] || die "pack_bmod.sh nao imprimiu um .bmod existente: '$BMOD'"

# Extrai o mod.so do .bmod para inspeção (o .so dentro do zip, não o do disco).
python3 - "$BMOD" "$WORK/bmod.so" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    names = [n for n in z.namelist() if n.endswith(".so")]
    if not names:
        print("manifest invalido: o .bmod nao tem nenhum .so", file=sys.stderr)
        raise SystemExit(1)
    open(sys.argv[2], "wb").write(z.read(names[0]))
PY

# --- 2. o helper (o mesmo caminho do .bmod, do deploy e do APK) ------------
echo "ship-stripped: (2) symbols_ship (o caminho real dos 3 consumidores)"
symbols_ship "$SRC" "$WORK/ship.so" "$WORK/symbols" ||
    die "symbols_ship recusou o .so de build (esperado: ele tem simbolo)"

# --- 3. stage do deploy / assets do APK: o MESMO .so do helper --------------
# O deploy_mod.sh e o manager/build.sh chamam symbols_ship direto, então o que
# eles entregam é exatamente o arquivo verificado aqui.
echo "ship-stripped: (3) stage do deploy e assets do APK usam o mesmo helper"

# --- checks sobre os .so entregues -----------------------------------------
check_so() {
    local label="$1" f="$2"
    local sec bid
    sec="$(mktemp)"
    "$BINDIR/llvm-readelf" -S "$f" >"$sec" 2>/dev/null || true
    if grep -q '\.symtab' "$sec"; then
        printf '  [FAIL] %s tem .symtab (%s bytes): simbolo indo para o usuario\n' \
            "$label" "$(stat -c%s "$f")"
        fail=1
    else
        printf '  [PASS] %s sem .symtab (%s bytes contra %s do .so de build)\n' \
            "$label" "$(stat -c%s "$f")" "$SRC_SIZE"
    fi
    rm -f "$sec"
    # O build-id precisa sobreviver ao strip: é ele que liga o tombstone do
    # aparelho aos símbolos guardados em symbols/.
    bid="$(symbols_build_id "$f")"
    if [ -n "$SRC_BID" ] && [ "$bid" = "$SRC_BID" ]; then
        printf '  [PASS] %s preserva o build-id (%s)\n' "$label" "$(printf '%.12s' "$bid")"
    else
        printf '  [FAIL] %s mudou o build-id (%s != %s): o crash do aparelho nao cruzaria\n' \
            "$label" "$bid" "$SRC_BID"
        fail=1
    fi
    # E tem que ter encolhido de verdade.
    if [ "$(stat -c%s "$f")" -ge "$SRC_SIZE" ]; then
        printf '  [FAIL] %s nao encolheu: o strip nao rodou\n' "$label"
        fail=1
    fi
}

check_so "mod.so dentro do .bmod" "$WORK/bmod.so"
check_so "mod.so do stage do deploy / assets do APK" "$WORK/ship.so"

# --- 4. os simbolos guardados tem que ser o NAO-stripado -------------------
echo "ship-stripped: (4) symbols/ guarda o não-stripado"
GOT="$WORK/symbols/$SRC_BID/lib$TARGET.so"
[ -f "$GOT" ] || die "symbols/ nao guardou o .so não-stripado: $GOT"
"$BINDIR/llvm-readelf" -S "$GOT" >"$WORK/got.sec" 2>/dev/null || true
if grep -q '\.symtab' "$WORK/got.sec"; then
    printf '  [PASS] symbols/ tem o .so com .symtab (%s bytes)\n' "$(stat -c%s "$GOT")"
else
    printf '  [FAIL] symbols/ guardou um .so sem simbolo: nao serve para simbolizar crash\n'
    fail=1
fi

# --- 4b. TODO .so entregue tem símbolo guardado, com o MESMO build-id ------
#
# ACHADO DA REVISÃO (f59e9ff, MEDIA): manager/build.sh chamava o symbols_ship
# com DOIS argumentos, sem raiz de símbolos. O u_dump.so saía stripped (certo)
# e o não-stripado era DESCARTADO — então o APK entregava uma biblioteca nativa
# de que ninguém tinha símbolo, e é justamente a que roda dentro do processo do
# jogo. Todo o resto que é entregue tinha símbolo; o do APK era o único de fora.
#
# Este check é GENÉRICO de propósito: ele varre TODOS os .so entregues que
# existem no disco e exige, para cada um, um símbolo guardado com o mesmo
# build-id. Não é um teste só pro u_dump — se amanhã entrar mais um .so no APK
# ou na release sem passar por symbols_ship, ele aparece aqui sozinho.
#
# Cobre: .so dentro do .bmod, o do stage do deploy, o dos assets do APK, e o da
# release (mods/). A lista vem do que existe, não do que o teste conhece.
echo "ship-stripped: (4b) todo .so entregue tem símbolo com o MESMO build-id"
ENTREGUES=0
FALTA=0
while IFS= read -r entregue; do
    [ -f "$entregue" ] || continue
    ENTREGUES=$((ENTREGUES + 1))
    bid="$(symbols_build_id "$entregue")"
    rel="$entregue"
    case "$entregue" in
        "$WORK"/*) rel="(stage do deploy) ${entregue#$WORK/}" ;;
        "$ROOT"/*) rel="${entregue#$ROOT/}" ;;
    esac
    if [ -z "$bid" ] || [ "$bid" = "0" ]; then
        printf '  [FAIL] %s sem NT_GNU_BUILD_ID: nao da para casar simbolo\n' "$rel"
        FALTA=$((FALTA + 1))
        continue
    fi
    if [ ! -d "$WORK/symbols/$bid" ] || [ -z "$(find "$WORK/symbols/$bid" -name '*.so' -print -quit 2>/dev/null)" ]; then
        printf '  [FAIL] %s (build-id %s) NAO tem simbolo guardado\n' "$rel" "$(printf '%.12s' "$bid")"
        FALTA=$((FALTA + 1))
        continue
    fi
    sym="$(find "$WORK/symbols/$bid" -name '*.so' -print -quit)"
    "$BINDIR/llvm-readelf" -S "$sym" >"$WORK/sym.sec" 2>/dev/null || true
    if ! grep -q '\.symtab' "$WORK/sym.sec"; then
        printf '  [FAIL] %s: o simbolo guardado para %s esta SEM .symtab\n' "$rel" "$(printf '%.12s' "$bid")"
        FALTA=$((FALTA + 1))
        continue
    fi
    printf '  [PASS] %s tem simbolo com o mesmo build-id (%s)\n' "$rel" "$(printf '%.12s' "$bid")"
done < <(find "$WORK" "$ROOT/mods" "$ROOT/out" -name '*.so' -not -path '*/obj/*' \
    -not -path '*/libs/*' -not -path '*/symbols/*' 2>/dev/null)
if [ "$ENTREGUES" -eq 0 ]; then
    die "nenhum .so entregue encontrado para conferir: o teste nao provar nada"
fi
check "todo .so entregue ($ENTREGUES) tem simbolo com o mesmo build-id" "$FALTA"

# --- 5. nenhum consumidor de libs/arm64-v8a/*.so copia o .so cru ----------
# A lista é o conjunto de tudo que a varredura encontrou. Se um consumidor novo
# aparecer, a lista tem que ser atualizada — e este check garante que nenhum dos
# listados faz `cp` direto do .so de build para um artefato de entrega.
echo "ship-stripped: (5) nenhum consumidor copia o .so cru para a entrega"
RAW_COPY=0
for f in tools/pack_bmod.sh tools/deploy_mod.sh manager/build.sh tools/build_module.sh \
         tools/build_release.sh; do
    [ -f "$ROOT/$f" ] || continue
    touch "$ROOT/$f"
    # (a) `cp <algo>/libs/arm64-v8a/<algo>.so <dest>` é o padrão perigoso. O
    #     pack_bmod tem um `cp` do manifest, que é permitido.
    grep -nE '^[[:space:]]*cp[[:space:]]+[^|]*libs/arm64-v8a/[^ ]*\.so' "$ROOT/$f" \
        >"$WORK/raw" 2>/dev/null || true
    if [ -s "$WORK/raw" ]; then
        printf '  [FAIL] %s copia o .so de build direto:\n' "$f"
        sed 's/^/        /' "$WORK/raw"
        RAW_COPY=1
    fi
    # (b) O pack_bmod empacota por ZINADOIRE dentro de um heredoc python, sem
    #     `cp`. É o caminho pelo qual a sabotagem do .bmod CRU passa, então
    #     ele é verificado à parte: nenhum `z.write(...)` pode receber um
    #     caminho que aponte para libs/arm64-v8a de dentro do python.
    if grep -nE 'z\.write\([^,]*(libs/arm64-v8a)' "$ROOT/$f" >"$WORK/zip" 2>/dev/null; then
        printf '  [FAIL] %s empacota o .so de build cru no zip:\n' "$f"
        sed 's/^/        /' "$WORK/zip"
        RAW_COPY=1
    fi
    # (c) quem mexe em libs/arm64-v8a tem que CHAMAR o helper. A chamada, e não
    #     a palavra: um comentário que explica o symbols_ship sobrevive à
    #     sabotagem, e foi assim que este check passou com o .so cru no .bmod.
    if grep -q 'libs/arm64-v8a' "$ROOT/$f" 2>/dev/null &&
       ! grep -qE '^[[:space:]]*symbols_ship[[:space:]]' "$ROOT/$f" 2>/dev/null; then
        printf '  [FAIL] %s mexe em libs/arm64-v8a mas nao CHAMA symbols_ship\n' "$f"
        RAW_COPY=1
    fi
done
check "nenhum consumidor copia o .so de build cru" "$RAW_COPY"

# --- 5b. o caminho de RELEASE passa RAIZ DE SÍMBOLOS --------------------------
#
# É o que cobre o caso do u_dump.so sem buildar o APK aqui (o APK do gate exige
# o SDK e demora). Sem o terceiro argumento o .so não-stripado é DESCARTADO, e o
# APK entrega uma biblioteca nativa de que ninguém tem símbolo — que foi
# exatamente o achado: manager/build.sh chamava com dois argumentos.
#
# Só o caminho de RELEASE é exigido: build_release (que monta o diretório da
# release), build_module (o loader do zip Magisk) e manager/build (os assets do
# APK) têm um arquivo de símbolos para onde guardar. pack_bmod e deploy_mod são
# ferramentas de DESENVOLVEDOR — o .bmod vai para outra pessoa e o deploy vai
# para o aparelho de quem está testando; não há arquivo de símbolos para nenhum
# dos dois, e inventar um seria guardar 1,8 MB à toa.
echo "ship-stripped: (5b) o caminho de release guarda símbolo do que publica"
SINROOT=0
for f in manager/build.sh tools/build_release.sh tools/build_module.sh; do
    [ -f "$ROOT/$f" ] || continue
    while IFS= read -r linha; do
        # NÃO usar IFS=: para separar "NN:conteudo": a linha pode ter mais
        # dois-pontos dentro ("${SYMBOLS_DIR:-}") e o campo saía partido.
        ln="${linha%%:*}"
        # a chamada pode continuar na linha seguinte; junta as 3
        corpo="$(sed -n "${ln},$((ln + 2))p" "$ROOT/$f")"
        # a chamada pode ocupar DUAS linhas ("\\" no fim da primeira), então os
        # argumentos são contados no corpo inteiro — mas só a partir da linha
        # da chamada, e sem as linhas de comentário (que citam symbols_ship sem
        # chamar).
        ult="$(printf '%s' "$corpo" | grep -m1 -E '^[[:space:]]*symbols_ship ')"
        [ -n "$ult" ] || continue
        chamada="$ult"
        # se a primeira linha acabou em \, a continuação entra na contagem
        case "$ult" in *\\) chamada="$ult $(printf '%s' "$corpo" | sed -n '2p')" ;; esac
        nargs="$(printf '%s' "$chamada" | sed 's/.*symbols_ship //' | grep -o '"[^"]*"' | wc -l)"
        if [ "$nargs" -lt 3 ]; then
            printf '  [FAIL] %s:%s publica um .so e NAO guarda simbolo (%s argumento(s))\n' \
                "$f" "$ln" "$nargs"
            SINROOT=1
        else
            printf '  [PASS] %s:%s guarda simbolo do que publica\n' "$f" "$ln"
        fi
    done < <(grep -nE '^[[:space:]]*symbols_ship ' "$ROOT/$f" 2>/dev/null || true)
done
check "todo .so publicado tem simbolo guardado" "$SINROOT"

[ "$fail" -eq 0 ] || die "simbolo vazando para o usuario (ver acima)"
echo "ship-stripped: OK (bmod, deploy e APK sem .symtab; $SRC_SIZE -> $(stat -c%s "$WORK/bmod.so") bytes)"
exit 0
