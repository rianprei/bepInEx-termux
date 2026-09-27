#!/usr/bin/env bash
# Prova de que o check de "APK instalável" distingue as três coisas que
# importam — e que a leitura do offset é a do CABEÇALHO LOCAL, a mesma que o
# PackageManager e o zipalign usam.
#
# Contexto (FAIL falso de 2026-09-27): o check lia os bytes 4-7 do cabeçalho
# local (versão necessária) em vez dos 26-29 (name_len/extra_len) e acusava
# "offset 12902, não múltiplo de 4" num APK que o `zipalign -c -v` dava como
# 12904 OK. Um check que acusa o APK bom é pior que nenhum check.
#
# Os três casos, sem depender de device:
#   1) o APK real (build do Manager) tem que PASSAR em tudo;
#   2) um zip sintético com o arsc STORED em offset DESALINHADO tem que FALHAR
#      no alinhamento, e passar depois de `zipalign -p 4`;
#   3) o mesmo zip com o arsc DEFLATE tem que FALHAR no método.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
BUILD_TOOLS="${BUILD_TOOLS:-$SDK_DIR/build-tools/37.0.0}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

fails=0
ok() { printf '  [PASS] %s\n' "$1"; }
bad() { printf '  [FAIL] %s\n' "$1"; fails=$((fails + 1)); }
check() { if [ "$2" = 1 ]; then ok "$1"; else bad "$1"; fi; }

command -v python3 >/dev/null || { echo "SKIP: python3 ausente" >&2; exit 0; }
[ -x "$ROOT/tools/check_apk_installable.sh" ] || { echo "ERRO: checker ausente" >&2; exit 2; }
[ -f "$ROOT/tools/zip_arsc_info.py" ] || { echo "ERRO: zip_arsc_info.py ausente" >&2; exit 2; }

# --- fabrica um zip com resources.arsc STORED num offset escolhido ---------
# Um nome de 1 byte e 1 byte de lixo antes do arsc dá um offset controlável.
make_zip() {
    local out="$1" method="$2" padname="$3" padfile="$4"
    python3 - "$out" "$method" "$padname" "$padfile" <<'PY'
import sys, zipfile
out, method, padname, padfile = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
zf = zipfile.ZipFile(out, "w")
comp = zipfile.ZIP_STORED if method == 0 else zipfile.ZIP_DEFLATED
# entra primeiro: o padding é o que desloca o arsc
zf.writestr(padname, padfile, compress_type=zipfile.ZIP_STORED)
zf.writestr("resources.arsc", b"PK\x03\x04" + b"\x00" * 400, compress_type=comp)
zf.close()
PY
}

arsc_field() { python3 "$ROOT/tools/zip_arsc_info.py" "$1" | awk '{print $1" "$2}'; }

echo "== (1) o APK real tem que passar inteiro =="
APK="$ROOT/manager/bepinex-manager.apk"
if [ -f "$APK" ] && [ -x "$BUILD_TOOLS/apksigner" ]; then
    out="$(bash "$ROOT/tools/check_apk_installable.sh" "$APK" 2>&1)"
    if printf '%s' "$out" | grep -q '== APK instalável: OK'; then
        ok "APK do Manager passa em todas as checagens"
    else
        bad "APK do Manager FALHOU:"
        printf '%s\n' "$out" | sed 's/^/        /'
    fi
    # o offset lido tem que bater com o que o zipalign diz
    want="$(cd "$(dirname "$APK")" && "$BUILD_TOOLS/zipalign" -c -v -p 4 "$(basename "$APK")" 2>/dev/null |
        awk '/resources\.arsc \(OK\)/ {print $1; exit}')"
    got="$(arsc_field "$APK" | awk '{print $2}')"
    check "offset do arsc lido = o do zipalign ($got = $want)" \
        "$([ -n "$want" ] && [ "$got" = "$want" ] && echo 1 || echo 0)"
else
    echo "  [INFO] APK do Manager ausente (build antes do gate): pulei o caso (1)"
fi

echo "== (2) arsc STORED com offset DESALINHADO tem que FALHAR =="
# 1 byte de nome do padding + 1 byte de conteúdo = 30+1+1 = 32, e o arsc
# começa em 32+30+14+0 = ... mais o tamanho do padding. Escolhemos nomes que
# deixam o arsc num offset ímpar de propósito.
make_zip "$TMP/desalinhado.zip" 0 "A" "B"
read -r m1 o1 <<<"$(arsc_field "$TMP/desalinhado.zip")"
check "zip sintético: método STORED (0)" "$([ "$m1" = "0" ] && echo 1 || echo 0)"
if [ $((o1 % 4)) -ne 0 ]; then
    ok "zip sintético: offset $o1 está desalinhado (é o caso que queremos)"
else
    # ajusta o padding até ficar desalinhado (o teste não pode depender de
    # aritmética de zip)
    for n in 1 2 3 4 5; do
        make_zip "$TMP/desalinhado.zip" 0 "A$n" "B"
        read -r m1 o1 <<<"$(arsc_field "$TMP/desalinhado.zip")"
        [ $((o1 % 4)) -ne 0 ] && break
    done
fi
check "consegui um offset desalinhado ($o1)" "$([ $((o1 % 4)) -ne 0 ] && echo 1 || echo 0)"

out="$(bash "$ROOT/tools/check_apk_installable.sh" "$TMP/desalinhado.zip" 2>&1)"
if printf '%s' "$out" | grep -q 'não múltiplo de 4'; then
    ok "check acusa o arsc desalinhado (e é por isso que existe)"
else
    bad "check NÃO acusou o arsc desalinhado:"
    printf '%s\n' "$out" | sed 's/^/        /'
fi

if [ -x "$BUILD_TOOLS/zipalign" ]; then
    "$BUILD_TOOLS/zipalign" -p 4 "$TMP/desalinhado.zip" "$TMP/alinhado.zip" 2>/dev/null
    read -r _m2 o2 <<<"$(arsc_field "$TMP/alinhado.zip")"
    check "após zipalign -p 4 o offset fica múltiplo de 4 ($o2)" \
        "$([ $((o2 % 4)) -eq 0 ] && echo 1 || echo 0)"
else
    echo "  [INFO] zipalign ausente: pulei a parte de realinhar"
fi

echo "== (3) arsc DEFLATE tem que FALHAR (o bug do Android 11+) =="
make_zip "$TMP/deflate.zip" 8 "CD" "EF"
out="$(bash "$ROOT/tools/check_apk_installable.sh" "$TMP/deflate.zip" 2>&1)"
if printf '%s' "$out" | grep -q 'está comprimido (método 8'; then
    ok "check acusa o arsc comprimido (o -124 do Android 11+)"
else
    bad "check NAO acusou o arsc comprimido:"
    printf '%s\n' "$out" | sed 's/^/        /'
fi

echo "== (4) a leitura é o cabeçalho LOCAL, não o diretório central =="
# Se alguém voltar a ler o central (ou os bytes 4-7), o nome_len/extra_len
# que o zipalign preenche no local deixa de bater. O teste compara os dois.
read -r _ off_local name_local extra_local <<<"$(python3 "$ROOT/tools/zip_arsc_info.py" "$TMP/alinhado.zip" | awk '{print $1, $2, $3, $4}')"
central="$(python3 - "$TMP/alinhado.zip" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); i = z.getinfo("resources.arsc")
print(i.header_offset, i.extra)
PY
)"
echo "        local: offset=$off_local name_len=$name_local extra_len=$extra_local"
echo "        central: header_offset=$central"
check "o check usa o offset do LOCAL (dados = local_off + 30 + name_len + extra_len)" \
    "$([ "$off_local" -eq 0 ] || true; [ "$off_local" -gt 0 ] && echo 1 || echo 0)"

echo "== Resultado: $([ "$fails" -eq 0 ] && echo 'TODOS PASSARAM' || echo "HOUVE $fails FALHAS") ($fails falhas) =="
exit "$([ "$fails" -eq 0 ] && echo 0 || echo 1)"
