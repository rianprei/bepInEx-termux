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

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -n "$APK" ] && [ -f "$APK" ] || { echo "uso: $0 <apk> [versionCode]" >&2; exit 2; }
[ -f "$ROOT/tools/zip_arsc_info.py" ] || { echo "ERRO: tools/zip_arsc_info.py ausente" >&2; exit 2; }
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
#    múltiplo de 4. Comprimido, o Android 11+ não instala (-124).
#    O offset vem do CABEÇALHO LOCAL (bytes 26-29 = name_len/extra_len), que é
#    o que o PackageManager lê e o que o zipalign alinha. Ler os bytes 4-7 (a
#    versão) dava 12902 num APK que o zipalign dizia estar em 12904 — FAIL
#    falso (achado do OpenCode, 2026-09-27). A leitura mora em
#    tools/zip_arsc_info.py, que o host test exercita com zips sintéticos.
read -r ARSC_METHOD ARSC_OFF _ARSC_NAME _ARSC_EXTRA < <(python3 "$ROOT/tools/zip_arsc_info.py" "$APK")
if [ "$ARSC_METHOD" = "0" ]; then
    ok "resources.arsc STORED (método 0)"
else
    bad "resources.arsc está comprimido (método $ARSC_METHOD, 0=STORED) — o Android 11+ recusa com INSTALL_PARSE_FAILED (-124)"
fi
if [ -z "$ARSC_OFF" ] || [ "$ARSC_OFF" = "-1" ]; then
    bad "não consegui ler o offset de resources.arsc (cabeçalho local ilegível?)"
elif [ $((ARSC_OFF % 4)) -eq 0 ]; then
    ok "resources.arsc alinhado em 4 (offset $ARSC_OFF, cabeçalho local)"
else
    bad "resources.arsc com offset $ARSC_OFF, não múltiplo de 4"
fi

# 3) assinatura: apksigner tem que verificar. -v mostra o esquema (v2 para
#    minSdk 26; v1 é só para < 24 e não é exigido aqui).
verify_out="$("$BUILD_TOOLS/apksigner" verify --min-sdk-version 26 -v "$APK" 2>&1)"
if "$BUILD_TOOLS/apksigner" verify --min-sdk-version 26 "$APK" >/dev/null 2>&1; then
    if grep -q 'Verified using v2 scheme (APK Signature Scheme v2): true' <<<"$verify_out"; then
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
