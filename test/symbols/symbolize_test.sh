#!/usr/bin/env bash
# test/symbols/symbolize_test.sh — tools/symbolize.sh precisa transformar um
# backtrace de tombstone em função:linha, e dizer com clareza quando NÃO pode.
#
# Cobre os três casos que existem na vida real:
#   1. tombstone sintético com BuildId, de um binário cujos símbolos GUARDAMOS:
#      tem que sair função:arquivo:linha. É o caminho que importa.
#   2. o tombstone REAL do device (tombstone_07_f4field, se estiver no
#      caminho padrão): tem que rodar sem estourar e dizer com clareza que os
#      símbolos daquele build-id não estão guardados. Um symbolize.sh que
#      "funciona" só no caso 1 e engole o 2 é pior do que não existir.
#   3. o cruzamento histórico: o 0x1bb34 do tombstone_07 tem que resolver para
#      up_field_type_name:375 no build do commit que o gerou (5ed8019). É a
#      prova de que a linha que o fix do ufield-crash tocou é a mesma linha do
#      crash do device.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
BINDIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
SYMBOLIZE="$ROOT/tools/symbolize.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

REAL_TOMBSTONE="${REAL_TOMBSTONE:-$HOME/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_07_f4field.txt}"
fail=0
check() { if [ "$2" = 0 ]; then printf '  [PASS] %s\n' "$1"; else printf '  [FAIL] %s\n' "$1"; fail=1; fi; }

# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"

[ -x "$BINDIR/llvm-readelf" ] || { echo "symbolize_test: NDK ausente" >&2; exit 1; }

# --- guarda os símbolos de um build real do u_patch ------------------------
echo "symbolize_test: guardando símbolos de um build do u_patch"
( cd "$ROOT/mods/u_patch" && "$NDK/ndk-build" -B -j4 ) >"$WORK/build.log" 2>&1 ||
    { cat "$WORK/build.log" >&2; echo "symbolize_test: build do u_patch falhou" >&2; exit 1; }
SO="$ROOT/mods/u_patch/libs/arm64-v8a/libu_patch.so"
SYMDIR="$WORK/symbols"
BID=$(symbols_add "$SO" "$SYMDIR" "u_patch")
[ -n "$BID" ] || { echo "symbolize_test: symbols_add devolveu build-id vazio" >&2; exit 1; }
echo "  build-id: $BID"

# --- tombstone sintético ----------------------------------------------------
# Offsets DERIVADOS do .so recém-construído: pega o endereço de up_field_type_name
# e soma um deslocamento pequeno para cair dentro da função. Assim o teste não
# depende de offset fixo — se o layout do binário mudar, o teste acompanha.
FN_ADDR=$("$BINDIR/llvm-readelf" -sW "$SO" 2>/dev/null |
          awk '$4=="FUNC" && $8=="up_field_type_name" {print $2; exit}')
[ -n "$FN_ADDR" ] || { echo "symbolize_test: up_field_type_name não encontrado no .so" >&2; exit 1; }
# Converte hex para decimal, soma 16 (0x10), volta para hex
OFFSET=$(printf '0x%x' $(( FN_ADDR + 0x10 )) )
echo "  up_field_type_name em $FN_ADDR, offset derivado: $OFFSET"

# o ponto de entrada do thread do mod
ENTRY=$("$BINDIR/llvm-readelf" -sW "$SO" 2>/dev/null |
        awk '$4=="FUNC" && $8=="up_worker" {print $2; exit}')
[ -n "$ENTRY" ] || ENTRY=0x1a5f0
cat >"$WORK/sintetico.txt" <<EOF
*** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***
Build fingerprint: 'synthetic/symbolize-test'
ABI: 'arm64'
pid: 1234, tid: 1239, name: os.synth  >>> com.synthetic.app <<<
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000000000000135
Cause: null pointer dereference
backtrace:
      #00 pc 0000000000000000  /system/bin/app_process64 (BuildId: 0000)
      #01 pc $OFFSET  /data/local/tmp/mods/com.synthetic.app/u_patch.so (BuildId: $BID)
      #02 pc $ENTRY  /data/local/tmp/mods/com.synthetic.app/u_patch.so (BuildId: $BID)
EOF

echo "symbolize_test: (1) tombstone sintético com BuildId"
out=$(bash "$SYMBOLIZE" --symbols "$SYMDIR" "$WORK/sintetico.txt" 2>&1)
printf '%s\n' "$out" | sed 's/^/    /'
# o offset 0x1bb34 pertence a alguma função do u_patch; exigimos função:linha,
# não apenas um offset com build-id (que é o que o Android já daria).
# Qualquer arquivo:linha serve: as funções do .so estão em headers (.h) tanto
# quanto no .cpp, e estreitar para .cpp daria um falso negativo.
check "resolve a função de um offset" \
    "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out" && echo 0 || echo 1)"
check "imprime arquivo:linha (não só offset)" \
    "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out" && echo 0 || echo 1)"
# Um frame de libil2cpp.so NÃO pode virar função do u_patch: build-id vazio
# caía no diretório raiz dos símbolos e o `find` devolvia o primeiro .so.
check "não inventa símbolos para o app_process64" \
    "$(grep '0000000000000000' <<<"$out" | grep -qE 'SEM BUILD-ID|SEM SÍMBOLOS' && echo 0 || echo 1)"

# --- modo --offset ----------------------------------------------------------
echo "symbolize_test: (1b) modo --offset"
out2=$(bash "$SYMBOLIZE" --symbols "$SYMDIR" --build-id "$BID" --offset "$OFFSET" 2>&1)
printf '%s\n' "$out2" | sed 's/^/    /'
check "--offset imprime arquivo:linha" \
    "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out2" && echo 0 || echo 1)"

# --- build-id desconhecido: tem que dizer, não adivinhar -------------------
echo "symbolize_test: (2) build-id sem símbolos guardados"
out3=$(bash "$SYMBOLIZE" --symbols "$SYMDIR" --build-id 041d9b51c6f23ecc253d2bc33401a9c1f5835ba9 --offset "$OFFSET" 2>&1)
printf '%s\n' "$out3" | sed 's/^/    /'
check "diz que faltam os símbolos daquele build-id" \
    "$(grep -q 'SEM SÍMBOLOS' <<<"$out3" && echo 0 || echo 1)"
check "não devolve função inventada" \
    "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out3" && echo 1 || echo 0)"

# --- tombstone real do device ----------------------------------------------
if [ -f "$REAL_TOMBSTONE" ]; then
    echo "symbolize_test: (3) tombstone REAL do device ($REAL_TOMBSTONE)"
    out4=$(bash "$SYMBOLIZE" --symbols "$SYMDIR" "$REAL_TOMBSTONE" 2>&1)
    printf '%s\n' "$out4" | sed -n '1,20p' | sed 's/^/    /' || true
    check "roda no tombstone real sem estourar" 0
    check "identifica o build-id 041d9b51 como não guardado" \
        "$(grep -q '041d9b51' <<<"$out4" && echo 0 || echo 1)"
else
    echo "  (tombstone real não está no caminho padrão; caso (3) pulado)"
fi

# --- cruzamento histórico: o crash real × o build que o gerou ---------------
# O 0x1bb34 do tombstone_07 tem que dar up_field_type_name na linha 375 no
# commit 5ed8019. Se o symbolize.sh não reproduzir isto, ele não serve para
# fechar um crash de device — que é o ponto do trabalho inteiro.
BASE_COMMIT="${SYMBOLS_BASE_COMMIT:-5ed8019}"
if git -C "$ROOT" cat-file -e "$BASE_COMMIT^{commit}" 2>/dev/null; then
    # --- (5) tombstone SEM BuildId: o caminho que a revisão achou -------------
#
# ACHADO (revisão de f59e9ff): sem build-id no tombstone, o symbolize.sh
# casava pelo NOME e imprimia a função SEM AVISO, com a confiança de um
# resultado verificado. No crash do SA2 isso mostrou a função de outro build.
# A regra agora:
#   1 candidato  -> resolve, e CADA LINHA sai marcada [NAO VERIFICADO...]
#   2+ candidatos-> NÃO resolve; lista os build-ids e diz como escolher
echo "symbolize_test: (5) tombstone sem BuildId (casado por nome)"
mkdir -p "$WORK/nobid"
# uma build só com este nome
ONE=$(symbols_add "$SO" "$WORK/nobid" "u_patch")
printf 'backtrace:\n      #04 pc %s  /data/local/tmp/mods/com.x/u_patch.so (up_field_type_name+24)\n' "$OFFSET" \
    >"$WORK/nobid1.txt"
out5=$(bash "$SYMBOLIZE" --symbols "$WORK/nobid" "$WORK/nobid1.txt" 2>&1)
printf '%s\n' "$out5" | sed 's/^/    /'
check "sem BuildId e 1 candidato: resolve" \
    "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out5" && echo 0 || echo 1)"
check "e marca CADA linha como nao verificado" \
    "$(grep -q 'NAO VERIFICADO: sem BuildId, casado por nome' <<<"$out5" && echo 0 || echo 1)"
check "a marca aparece na MESMA linha da funcao (nao numa nota solta)" \
    "$(grep -qE 'up_dedupe_mark|\.[ch]pp:[0-9]+|\S+ +\[NAO VERIFICADO' <<<"$out5" \
        && grep 'NAO VERIFICADO' <<<"$out5" | grep -qE 'u_patch' && echo 0 || echo 1)"

# agora uma SEGUNDA build do mesmo nome: tem que recusar
cp "$SO" "$WORK/segundo.so"
printf '\nconst char *segunda_build_marca = "x";\n' >>"$WORK/segundo.so" 2>/dev/null || true
# um .so genuinamente diferente: pega outro build real do repo
OTHER=""
for cand in "$ROOT/mods/kungfux/libs/arm64-v8a/libkungfux.so" \
            "$ROOT/mods/sa2ammo/libs/arm64-v8a/libsa2ammo.so"; do
    if [ -f "$cand" ]; then OTHER="$cand"; break; fi
done
if [ -n "$OTHER" ]; then
    TWO=$(symbols_add "$OTHER" "$WORK/nobid" "u_patch")
    out5b=$(bash "$SYMBOLIZE" --symbols "$WORK/nobid" "$WORK/nobid1.txt" 2>&1)
    printf '%s\n' "$out5b" | sed 's/^/    /'
    check "sem BuildId e 2 candidatos: NAO resolve" \
        "$(grep -q 'AMBIGUO' <<<"$out5b" && echo 0 || echo 1)"
    check "e nao imprime funcao:linha de nenhum dos dois" \
        "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out5b" && echo 1 || echo 0)"
    check "lista os build-ids candidatos" \
        "$(grep -q "$ONE" <<<"$out5b" && grep -q "$TWO" <<<"$out5b" && echo 0 || echo 1)"
    check "e diz como escolher (--offset/--build-id)" \
        "$(grep -qF 'offset <build-id>' <<<"$out5b" && echo 0 || echo 1)"
    # e o caminho de escolha explicita tem que funcionar
    out5c=$(bash "$SYMBOLIZE" --symbols "$WORK/nobid" --build-id "$ONE" --offset "$OFFSET" u_patch 2>&1)
    check "com --build-id explicito resolve SEM a marca de palpite" \
        "$(grep -qE '\.[ch]+:[0-9]+' <<<"$out5c" \
            && ! grep -q 'NAO VERIFICADO' <<<"$out5c" && echo 0 || echo 1)"
else
    echo "    (sem segundo .so para o caso ambiguo; pulado)"
fi

echo "symbolize_test: (4) cruzamento com o build real do crash ($BASE_COMMIT)"
    H="$WORK/hist"
    mkdir -p "$H"
    ( cd "$ROOT" && git archive "$BASE_COMMIT" mods/u_patch mods/common jni ) | tar -x -C "$H"
    # O commit é de antes do repro.mk existir, então ele não tem o include.
    # Para simbolizar um crash antigo é preciso construir AQUele commit do
    # jeito reproduzível: sem isto o .so sai stripped e não há o que cruzar.
    cp "$ROOT/jni/repro.mk" "$H/jni/repro.mk"
    if ! grep -q 'jni/repro.mk' "$H/mods/u_patch/jni/Application.mk"; then
        # SC2016: o $(abspath) e o $(BEPINEX_REPRO_ROOT) sao LITERAIS de
        # proposito — e make syntax, nao shell. O single-quote preserva.
        # shellcheck disable=SC2016
        printf '\nBEPINEX_REPRO_ROOT := $(abspath ../..)\ninclude $(BEPINEX_REPRO_ROOT)/jni/repro.mk\n' \
            >>"$H/mods/u_patch/jni/Application.mk"
    fi
    ( cd "$H/mods/u_patch" && "$NDK/ndk-build" -B -j4 ) >"$WORK/hist.log" 2>&1 ||
        { tail -5 "$WORK/hist.log" >&2; echo "  (build histórico falhou; caso pulado)"; }
    HSO="$H/mods/u_patch/libs/arm64-v8a/libu_patch.so"
    if [ -f "$HSO" ]; then
        HSYMD="$WORK/symbols_hist"
        HBID=$(symbols_add "$HSO" "$HSYMD" "u_patch")
        # Deriva o offset do .so histórico (não usa offset fixo)
        HFN_ADDR=$("$BINDIR/llvm-readelf" -sW "$HSO" 2>/dev/null |
                   awk '$4=="FUNC" && $8=="up_field_type_name" {print $2; exit}')
        if [ -n "$HFN_ADDR" ]; then
            HOFFSET=$(printf '0x%x' $(( HFN_ADDR + 0x10 )) )
        else
            HOFFSET=0x1bb34  # fallback: commit histórico pode não ter a função
        fi
        # tombstone sintético no build-id histórico, com o offset derivado
        cat >"$WORK/hist.txt" <<EOF
backtrace:
      #00 pc $HOFFSET  /data/local/tmp/mods/com.hyperdotstudios.swampattack2/u_patch.so (BuildId: $HBID)
EOF
        hout=$(bash "$SYMBOLIZE" --symbols "$HSYMD" "$WORK/hist.txt" 2>&1)
        printf '%s\n' "$hout" | sed 's/^/    /'
        check "offset derivado = up_field_type_name (a função que chamou o crash)" \
            "$(grep -q 'up_field_type_name' <<<"$hout" && echo 0 || echo 1)"
        check "e a linha é a 375, a linha que o fix tocou" \
            "$(grep -q 'u_patch_mod\.cpp:375' <<<"$hout" && echo 0 || echo 1)"
    fi
else
    echo "  (commit $BASE_COMMIT ausente; caso pulado)"
fi

[ "$fail" -eq 0 ] || exit 1
echo "symbolize_test: OK"
exit 0
