#!/usr/bin/env bash
# Empacota o módulo num zip instalável pelo app Magisk (ou KernelSU + ZygiskNext).
# Idempotente: roda quantas vezes quiser, sempre do mesmo jeito.
#
# Estrutura do zip (documentação oficial do Magisk, seção "Magisk Module
# Installer"): o zip É o módulo, mais META-INF/com/google/android/update-binary
# (o module_installer.sh baixado do Magisk, renomeado) e updater-script
# contendo só "#MAGISK".
#   https://topjohnwu.github.io/Magisk/guides.html
# O KernelSU usa o mesmo layout de pasta e as mesmas variáveis em customize.sh:
#   https://kernelsu.org/guide/module.html
#
# Uso: tools/build_module.sh   (sem adb, sem push — só gera o arquivo)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
OUT="$ROOT/out"
STAGE="$OUT/stage"
ZIP_IN="$OUT/module_installer.sh"
UPDATE_BINARY_URL="https://raw.githubusercontent.com/topjohnwu/Magisk/master/scripts/module_installer.sh"
NDK=${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}

# --- versão: UM lugar só --------------------------------------------------------
# VERSION = "<version> <versionCode>". O módulo mostra essa versão no app
# Magisk; o loader mostra a dele no logcat. Se divergirem o usuário instala
# "v0.4.0" e vê "módulo carregado — v0.3.6", então o build reclama.
read -r VERSION VERSION_CODE < VERSION
grep -q "BC_LOADER_VERSION \"$VERSION\"" jni/main.cpp || {
    echo "ERRO: VERSION diz $VERSION mas jni/main.cpp nao bate. Atualize o define." >&2
    exit 1
}

# --- 1. loader ------------------------------------------------------------------
# Zygisk exige zygisk/<abi>.so (o nome do módulo sai do nome do .so? não: o
# loader é carregado em todo app, não por nome de módulo — arm64-v8a.so basta).
[ -x "$NDK/ndk-build" ] || { echo "ERRO: ndk-build não encontrado em $NDK" >&2; exit 1; }
"$NDK/ndk-build" -B -j4 >/dev/null

# --- 2. staging (limpo a cada run) ---------------------------------------------
rm -rf "$STAGE" "$OUT/bepinex-termux-$VERSION.zip"
mkdir -p "$STAGE/zygisk" "$STAGE/META-INF/com/google/android"
cp libs/arm64-v8a/libbc-poc.so "$STAGE/zygisk/arm64-v8a.so"
# O resto do módulo vem de module/ (fonte da verdade); o zip é gerado, não editado.
cp module/sepolicy.rule module/post-fs-data.sh module/customize.sh module/uninstall.sh "$STAGE/"

# --- 3. instalador padrão do Magisk ---------------------------------------------
# A doc oficial manda baixar o module_installer.sh e renomear pra update-binary.
# Baixamos em build (em vez de versionar o arquivo, que é GPL-3.0 do Magisk
# dentro de um repo MIT) e cacheamos em out/ — o 2º build não usa a rede.
if [ ! -f "$ZIP_IN" ]; then
    echo "baixando module_installer.sh do Magisk..."
    curl -fsSL "$UPDATE_BINARY_URL" -o "$ZIP_IN" || {
        echo "ERRO: curl falhou. Baixe $UPDATE_BINARY_URL para $ZIP_IN." >&2
        exit 1
    }
fi
cp "$ZIP_IN" "$STAGE/META-INF/com/google/android/update-binary"
chmod 755 "$STAGE/META-INF/com/google/android/update-binary"
# updater-script tem que conter exatamente "#MAGISK" (doc oficial).
printf '#MAGISK\n' >"$STAGE/META-INF/com/google/android/updater-script"

# --- 4. module.prop (id mantido de propósito) -----------------------------------
# id=bc-poc para atualizar o módulo já instalado do usuário em vez de instalar
# um segundo módulo em paralelo. Trocar o id duplica a instalação.
cat >"$STAGE/module.prop" <<EOF
id=bc-poc
name=bepInEx-termux
version=$VERSION
versionCode=$VERSION_CODE
author=rianprei
description=Carrega mods nativos .so em jogos Android por runtime (Zygisk), sem tocar no APK. Pasta de mods basta, sem allowlist. Magisk ou KernelSU+ZygiskNext.
EOF

# --- 5. Manager (F5), opcional ---------------------------------------------------
# O Manager é de outra branch; se manager/out/*.apk existir ele entra no zip e o
# customize.sh instala. Ausente = zip só com o loader, e a impressão na tela
# avisa o usuário.
shopt -s nullglob
apks=(manager/out/*.apk)
shopt -u nullglob
if [ ${#apks[@]} -gt 0 ]; then
    cp "${apks[0]}" "$STAGE/manager.apk"
    echo "Manager incluído: ${apks[0]##*/}"
else
    echo "sem manager.apk (Manager ainda não mergeado) — o zip instala só o loader"
fi

# --- 6. zip ----------------------------------------------------------------------
# zip -r é o caminho normal; o zipfile do python3 é o plano B (o mesmo formato,
# mesma lista de entradas) pra máquina sem o binário zip.
ZIP="$OUT/bepinex-termux-$VERSION.zip"
(
    cd "$STAGE"
    if command -v zip >/dev/null 2>&1; then
        zip -qr9 "$ZIP" .
    else
        python3 -m zipfile -c "$ZIP" .
    fi
)
echo "pronto: $ZIP"
echo "  unzip -l \"$ZIP\""
echo "  instalar: copiar pro device e flashear pelo app Magisk (ou Recovery), depois reiniciar"
