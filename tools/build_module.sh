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
# Instalador pinado em commit, não em master: master muda sozinho e o
# module_installer.sh é quem instala o módulo. SHA256 conferido no build.
MAGISK_COMMIT="5b06d817d4ae5a916df88fd9efebf12537f40cdb"
UPDATE_BINARY_SHA256="bcf4b1d9913f3af17755569c853e0b5a75b8005f6a18eb3f86dadcc0e968c29d"
UPDATE_BINARY_URL="https://raw.githubusercontent.com/topjohnwu/Magisk/${MAGISK_COMMIT}/scripts/module_installer.sh"
NDK=${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}

# --- versão: UM lugar só --------------------------------------------------------
# VERSION = "<version> <versionCode>". O módulo mostra essa versão no app
# Magisk; o loader mostra a dele no logcat. Se divergirem o usuário instala
# "v0.4.0" e vê "módulo carregado — v0.3.6", então o build reclama.
version_line=$(cat VERSION)
read -r VERSION VERSION_CODE <<EOF
$version_line
EOF
grep -q "BC_LOADER_VERSION \"$VERSION\"" jni/main.cpp || {
    echo "ERRO: VERSION diz $VERSION mas jni/main.cpp nao bate. Atualize o define." >&2
    exit 1
}

# --- 1. instalador padrão do Magisk (primeiro: falha rápido, sem gastar build)------------------------------------------
# A doc oficial manda baixar o module_installer.sh e renomear pra update-binary.
# Baixamos em build (em vez de versionar o arquivo, que é GPL-3.0 do Magisk
# dentro de um repo MIT) e cacheamos em out/ — o 2º build não usa a rede.
# O out/ precisa existir ANTES do curl: -o escreve direto no arquivo, e sem o
# diretório o curl falha com "(23) client returned ERROR on write" — o que só
# aparece em checkout limpo, porque com out/ em cache o download nem roda.
mkdir -p "$OUT"
if [ ! -f "$ZIP_IN" ]; then
    echo "baixando module_installer.sh (Magisk ${MAGISK_COMMIT:0:12})..."
    curl -fsSL "$UPDATE_BINARY_URL" -o "$ZIP_IN" || {
        echo "ERRO: curl falhou. Baixe $UPDATE_BINARY_URL para $ZIP_IN." >&2
        exit 1
    }
fi
# Confere sempre, mesmo no cache: se o arquivo foi editado à mão ou o download
# foi interceptado, o build para aqui.
echo "${UPDATE_BINARY_SHA256}  ${ZIP_IN}" | sha256sum -c - >/dev/null || {
    echo "ERRO: module_installer.sh nao bate com o SHA256 pinado (Magisk ${MAGISK_COMMIT})." >&2
    exit 1
}

# --- 2. loader ------------------------------------------------------------------
# Zygisk exige zygisk/<abi>.so (o nome do módulo sai do nome do .so? não: o
# loader é carregado em todo app, não por nome de módulo — arm64-v8a.so basta).
[ -x "$NDK/ndk-build" ] || { echo "ERRO: ndk-build não encontrado em $NDK" >&2; exit 1; }
# Antes de compilar: o sepolicy.rule tem que estar na gramática do magiskpolicy,
# senão o Magisk aplica o arquivo em parte (statement ruim = warn e segue) e a
# política do módulo fica pela metade, sem ninguém avisar.
tools/check_sepolicy_rule.sh
BUILD_LOG="$OUT/build.log"
# Nada de >/dev/null: warning de compilação é sinal, e o único tolerado é o
# '-static-libstdc++' do NDK 23 (argumento sem uso na linkagem do clang).
if ! "$NDK/ndk-build" -B -j4 >"$BUILD_LOG" 2>&1; then
    cat "$BUILD_LOG" >&2
    echo "ERRO: ndk-build falhou (log em $BUILD_LOG)" >&2
    exit 1
fi
tail -n 3 "$BUILD_LOG"
unexpected=$(grep -E 'warning:|error:' "$BUILD_LOG" | grep -v -- '-static-libstdc++' || true)
if [ -n "$unexpected" ]; then
    printf '%s\n' "$unexpected" >&2
    echo "ERRO: warning/error inesperado no build (acima). Só o -static-libstdc++ é tolerado." >&2
    exit 1
fi

# --- 3. staging (limpo a cada run) ---------------------------------------------
rm -rf "$STAGE" "$OUT/bepinex-termux-$VERSION.zip"
mkdir -p "$STAGE/zygisk" "$STAGE/META-INF/com/google/android"
cp libs/arm64-v8a/libbc-poc.so "$STAGE/zygisk/arm64-v8a.so"
# O resto do módulo vem de module/ (fonte da verdade); o zip é gerado, não editado.
cp module/sepolicy.rule module/post-fs-data.sh module/customize.sh module/uninstall.sh \
   module/action.sh "$STAGE/"

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
# G7 (release imutável): o zip precisa sair BIT-IDÊNTICO em dois builds limpos.
# O `zip -qr9` não garante isso (mtime de cada entrada + ordem do readdir + campos
# extras de uid/gid), então o empacotamento é feito com o zipfile do python3, onde
# cada uma dessas coisas é explícita:
#   - ordem fixa: todas as entradas ordenadas por caminho;
#   - mtime fixo: SOURCE_DATE_EPOCH (default: timestamp do último commit);
#   - permissões normalizadas: 755 no que é executável, 644 no resto, sem uid/gid;
#   - nada de campo extra (equivalente ao -X do zip(1));
#   - compressão em nível fixo.
# Requer python3 (o repo já assume JDK/aapt2 do Manager; python3 é o mesmo tipo
# de dependência de build).
ZIP="$OUT/bepinex-termux-$VERSION.zip"
EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}
command -v python3 >/dev/null 2>&1 || { echo "ERRO: python3 é necessário pro zip determinístico" >&2; exit 1; }
python3 - "$STAGE" "$ZIP" "$EPOCH" <<'PYEOF'
import os
import stat
import sys
import time
import zipfile

stage, out, epoch = sys.argv[1], sys.argv[2], int(sys.argv[3])
date_time = time.gmtime(epoch)[0:6]

entries = []
for root, dirs, names in os.walk(stage):
    dirs.sort()
    for name in names:
        path = os.path.join(root, name)
        entries.append((os.path.relpath(path, stage).replace(os.sep, "/"), path))
entries.sort()

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for arc, path in entries:
        info = zipfile.ZipInfo(arc, date_time=date_time)
        info.create_system = 3  # unix: external_attr é modo
        info.compress_type = zipfile.ZIP_DEFLATED
        mode = 0o755 if os.stat(path).st_mode & stat.S_IXUSR else 0o644
        info.external_attr = mode << 16
        with open(path, "rb") as fh:
            zf.writestr(info, fh.read())
PYEOF
echo "pronto: $ZIP (SOURCE_DATE_EPOCH=$EPOCH, $(stat -c '%s' "$ZIP") bytes)"
sha256sum "$ZIP"
echo "  unzip -l \"$ZIP\""
echo "  instalar: copiar pro device e flashear pelo app Magisk (ou Recovery), depois reiniciar"
