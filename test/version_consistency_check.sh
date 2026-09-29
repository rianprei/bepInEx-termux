#!/usr/bin/env bash
# test/version_consistency_check.sh — uma versão só, e ela concorda em todo lugar.
#
# POR QUE ESTE CHECK EXISTE
#
# A versão do projeto tem UMA fonte, o arquivo VERSION da raiz, na forma
# "<nome> <versionCode>". O APK deriva dela (manager/build.sh:31-33 lê, e
# passa --version-code/--version-name ao aapt2 e gera o BuildConfig), e o
# AndroidManifest não tem versionCode de propósito. Ou seja: mexer no VERSION
# MUDA o APK inteiro, sem ninguém editar outro lugar.
#
# Isso deixa um buraco: o BC_LOADER_VERSION em jni/main.cpp é uma CÓPIA
# digitada à mão do nome da versão, e nada checava se as duas coisas
# concordavam. A cópia é o que diverge — o VERSION sobe, o APK sobe, e o
# loader continua anunciando a versão velha no device.
#
# O QUE ESTE CHECK COBRE
#   1. VERSION tem o formato "<nome> <versionCode>".
#   2. O versionCode segue o esquema do histórico: major*100 + minor
#      (v0.4.0=400, v0.4.1=401 — ver o histórico do arquivo VERSION).
#   3. BC_LOADER_VERSION bate com o nome do VERSION.
#   4. O CHANGELOG tem uma seção "## <nome>".
#   5. Se existir APK/módulo já construído, o nome bate com o VERSION.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT" || exit 1

fails=0
err() { echo "version-consistency: $*" >&2; fails=$((fails + 1)); }
ok()  { echo "  ok  $*"; }

# --- 1. o VERSION existe e tem o formato ------------------------------------
if [ ! -f VERSION ]; then
    err "VERSION ausente na raiz do repo"
    echo "=== version_consistency_check: $fails falha(s) ==="
    exit 1
fi
read -r VNAME VCODE < VERSION
[ -n "${VNAME:-}" ] && [ -n "${VCODE:-}" ] || { err "VERSION mal formado: '$(cat VERSION)'"; echo "=== version_consistency_check: $fails falha(s) ==="; exit 1; }
ok "VERSION = $VNAME $VCODE"

# --- 2. esquema: versionCode = <2o componente>*100 + <3o componente> ------
# Do histórico do arquivo VERSION: v0.4.0 -> 400 e v0.4.1 -> 401. Ou seja, o
# código é o SEGUNDO componente ("4") vezes 100, mais o TERCEIRO ("1"). O
# primeiro ("0") não entra. Daí v0.5.0 -> 5*100 + 0 = 500.
if [[ "$VNAME" =~ ^v?([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    # SC2034: o primeiro componente nao entra no esquema, por isso e lido e nao usado
    c2="${BASH_REMATCH[2]}"; c3="${BASH_REMATCH[3]}"
    esperado=$(( c2 * 100 + c3 ))
    if [ "$VCODE" -eq "$esperado" ]; then
        ok "esquema: $VNAME -> $VCODE = ${c2}*100 + ${c3}  (v0.4.0=400, v0.4.1=401)"
    else
        err "versionCode $VCODE fora do esquema para $VNAME (esperado $esperado = ${c2}*100 + ${c3})"
    fi
else
    err "nome de versão não reconhecido (esperado vX.Y.Z): $VNAME"
fi

# --- 3. o loader bate com o VERSION ----------------------------------------
loader=$(sed -n 's/^#define[[:space:]]\+BC_LOADER_VERSION[[:space:]]\+"\(.*\)".*/\1/p' jni/main.cpp | head -1)
if [ -z "$loader" ]; then
    err "BC_LOADER_VERSION não encontrado em jni/main.cpp"
elif [ "$loader" = "$VNAME" ]; then
    ok "BC_LOADER_VERSION ($loader) == VERSION ($VNAME)"
else
    err "BC_LOADER_VERSION é '$loader' e o VERSION é '$VNAME' — o loader anunciaria a versão errada no device"
fi

# --- 4. o CHANGELOG tem a seção da versão -----------------------------------
if grep -qE "^##[[:space:]]+v?${VNAME//./\\.}([[:space:]]|$)" CHANGELOG.md; then
    ok "CHANGELOG tem a seção '## $VNAME'"
else
    err "CHANGELOG não tem a seção '## $VNAME' — a versão subiu e o registro não"
fi

# --- 5. se houver artefato construído, o nome bate ------------------------
# Não é erro se não houver nada construído: o check tem que rodar no gate, que
# não constrói APK. Só confere quando o artefato existe.
achou=0
for d in out/release/*/; do
    [ -d "$d" ] || continue
    achou=1
    if [[ "$(basename "$d")" == "$VNAME" ]]; then
        ok "diretório de release $d corresponde ao VERSION"
    else
        err "diretório de release '$d' não corresponde ao VERSION ($VNAME)"
    fi
done
[ "$achou" -eq 0 ] && ok "nenhum release construído ainda (sem conference de artefato)"

echo "=== version_consistency_check: $fails falha(s) ==="
[ "$fails" -eq 0 ]
