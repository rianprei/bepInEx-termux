#!/bin/sh
# Baixa o apktool (que traz baksmali+smali) pinado por sha256. O patcher usa
# só as classes de smali dele, em `java -cp`: remontar um dex depois de mudar
# o method pool NÃO é patch de bytes (ver README do did-restore-wip).
#
# Por que apktool e não os jars soltos de baksmali/smali: o jar do apktool é
# um fat jar com as dependências já dentro (dexlib2, guava, antlr...). Os
# jars soltos do Maven puxam uma árvore de dependências na mão.
#
# Uso: sh tools/fetch_apktool.sh   (o caminho sai no stdout)
set -eu
VERSION=$(grep '^apktool-jar-sha256|' "$(dirname "$0")/deps.lock" | cut -d'|' -f2)
SHA256=$(grep '^apktool-jar-sha256|' "$(dirname "$0")/deps.lock" | cut -d'|' -f3)
URL="https://github.com/iBotPeaches/Apktool/releases/download/v${VERSION}/apktool_${VERSION}.jar"
OUT="$(dirname "$0")/../out/apktool"
mkdir -p "$OUT"
JAR="$OUT/apktool-${VERSION}.jar"
if [ ! -f "$JAR" ]; then
    curl -sL --fail -o "$JAR.tmp" "$URL" || { rm -f "$JAR.tmp"; echo "ERRO: download falhou" >&2; exit 1; }
    mv "$JAR.tmp" "$JAR"
fi
echo "${SHA256}  ${JAR}" | sha256sum -c - >/dev/null || { echo "ERRO: sha256 do apktool não bate (esperado $SHA256)" >&2; exit 1; }
echo "$JAR"
