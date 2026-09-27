#!/usr/bin/env bash
# "APK instalável": o que o Android 11+ exige de um APK assinado, checado
# no artefato final e não num theory.
#
# Por que isto existe: o build normalizava TODAS as entradas do APK com
# DEFLATE, e o Android 11+ recusa resources.arsc comprimido
# (INSTALL_PARSE_FAILED, -124: "resources.arsc is compressed"). O device
# zoado foi a prova (rodada de 2026-09-26); o host não pega isso sozinho.
#
# Uso: bash tools/check_apk_installable.sh <apk> [versionCode esperado]
# Sai != 0 com o motivo de cada falha. Usado pela etapa "APK instalavel"
# do verify_all e na mão quando um APK suspecto aparece.
set -uo pipefail

APK="${1:-}"
WANT_CODE="${2:-}"
SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
BUILD_TOOLS="${BUILD_TOOLS:-$SDK_DIR/build-tools/37.0.0}"

fails=0
ok() { printf '  [PASS] %s\n' "$1"; }
bad() { printf '  [FAIL] %s\n' "$1"; fails=$((fails + 1)); }

[ -n "$APK" ] && [ -f "$APK" ] || { echo "uso: $0 <apk> [versionCode]" >&2; exit 2; }
[ -x "$BUILD_TOOLS/zipalign" ] || { echo "ERRO: zipalign ausente ($BUILD_TOOLS)" >&2; exit 2; }
[ -x "$BUILD_TOOLS/apksigner" ] || { echo "ERRO: apksigner ausente ($BUILD_TOOLS)" >&2; exit 2; }
[ -x "$BUILD_TOOLS/aapt2" ] || { echo "ERRO: aapt2 ausente ($BUILD_TOOLS)" >&2; exit 2; }
command -v python3 >/dev/null || { echo "ERRO: python3 ausente" >&2; exit 2; }

# 1) alinhamento: -p = alinha as entradas de dados em 4 bytes (o Page-align
#    de antes do Android 11 exige), -c = só checa.
if "$BUILD_TOOLS/zipalign" -c -p 4 "$APK" >/dev/null 2>&1; then
    ok "zipalign -c -p 4 (entradas de dados alinhadas)"
else
    bad "zipalign -c -p 4 falhou (rode zipalign -p 4 antes de assinar)"
fi

# 2) resources.arsc: tem que estar STORED (método 0) e começar em offset
#    múltiplo de 4. Comprimido, o Android 11+ não instala (-124). O offset
#    vem do cabeçalho LOCAL (o que o PackageManager lê), então é esse que
#    importa — o diretório central pode discordar.
read -r ARSC_METHOD ARSC_OFF < <(python3 - "$APK" <<'PY'
import struct, sys, zipfile
apk = sys.argv[1]
with zipfile.ZipFile(apk) as z:
    info = z.getinfo("resources.arsc")
method = info.compress_type                      # 0 = STORED
with open(apk, "rb") as f:
    # offset do cabeçalho local, lido do diretório central
    f.seek(info.header_offset)
    sig = f.read(4)
    if sig != b"PK\x03\x04":
        print("-1 -1"); raise SystemExit(0)
    name_len, extra_len = struct.unpack("<HH", f.read(4))
    off = info.header_offset + 30 + name_len + extra_len
print(method, off)   # método e offset: o tamanho não é usado pelo check
PY
)
if [ "$ARSC_METHOD" = "0" ]; then
    ok "resources.arsc STORED (método 0)"
else
    bad "resources.arsc está comprimido (método $ARSC_METHOD, 0=STORED) — o Android 11+ recusa com INSTALL_PARSE_FAILED (-124)"
fi
if [ -n "$ARSC_OFF" ] && [ "$ARSC_OFF" -eq 0 ] 2>/dev/null; then
    bad "não consegui ler o offset de resources.arsc"
elif [ $((ARSC_OFF % 4)) -eq 0 ] 2>/dev/null; then
    ok "resources.arsc alinhado em 4 (offset $ARSC_OFF)"
else
    bad "resources.arsc com offset $ARSC_OFF, não múltiplo de 4"
fi

# 3) assinatura: apksigner tem que verificar. -v mostra o esquema (v2 para
#    minSdk 26; v1 é só para < 24 e não é exigido aqui).
verify_out="$("$BUILD_TOOLS/apksigner" verify --min-sdk-version 26 -v "$APK" 2>&1)"
if "$BUILD_TOOLS/apksigner" verify --min-sdk-version 26 "$APK" >/dev/null 2>&1; then
    if printf '%s' "$verify_out" | grep -q 'Verified using v2 scheme (APK Signature Scheme v2): true'; then
        ok "assinatura verificável com esquema v2 (minSdk 26)"
    else
        bad "assinatura verificada, mas sem o esquema v2 que o minSdk 26 exige"
    fi
else
    bad "apksigner verify falhou: $(printf '%s' "$verify_out" | head -n 1)"
fi

# 4) a versão do APK é a do VERSION da raiz (o Manager tem que ser
#    atualizável: versão errada = updateinstaller em vez de update).
badging="$("$BUILD_TOOLS/aapt2" dump badging "$APK" 2>/dev/null | head -n 1)"
code="$(printf '%s' "$badging" | sed -n "s/.*versionCode='\([0-9]*\)'.*/\1/p")"
if [ -z "$code" ]; then
    bad "aapt2 não leu versionCode do APK"
elif [ -n "$WANT_CODE" ] && [ "$code" != "$WANT_CODE" ]; then
    bad "versionCode do APK é $code, o VERSION da raiz diz $WANT_CODE"
else
    ok "aapt2 le versionCode=$code${WANT_CODE:+ (bate com o VERSION)}"
fi

if [ "$fails" -eq 0 ]; then
    echo "== APK instalável: OK (0 falhas) =="
    exit 0
fi
echo "== APK instalável: $fails FALHA(S) ==" >&2
exit 1
