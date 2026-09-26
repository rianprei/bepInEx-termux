#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/test_classes"

mkdir -p "${BUILD_DIR}"

# Mesma versao que o APK recebe: BuildVersion.java e gerado do VERSION da
# raiz nos dois scripts, entao o host e o APK nunca discordam da versao.
VERSION_FILE="${SCRIPT_DIR}/../VERSION"
read -r VERSION_CODE VERSION_NAME < <(bash "${SCRIPT_DIR}/version.sh" "${VERSION_FILE}")
echo "[*] Versao (VERSION da raiz): ${VERSION_NAME} (versionCode ${VERSION_CODE})"
GEN_DIR="${BUILD_DIR}/gen"
mkdir -p "${GEN_DIR}/io/github/rianprei/bepinex/manager/core"
cat > "${GEN_DIR}/io/github/rianprei/bepinex/manager/core/BuildVersion.java" <<JAVA_EOF
package io.github.rianprei.bepinex.manager.core;

// Gerado por manager/run_tests.sh a partir do VERSION da raiz do repo. Nao editar.
public final class BuildVersion {
    public static final String NAME = "${VERSION_NAME}";
    public static final int CODE = ${VERSION_CODE};
    private BuildVersion() {}
}
JAVA_EOF

echo "[*] Compilando codigo e testes com javac --release 17..."
javac --release 17 -d "${BUILD_DIR}" \
    "${GEN_DIR}"/io/github/rianprei/bepinex/manager/core/BuildVersion.java \
    "${SCRIPT_DIR}"/src/io/github/rianprei/bepinex/manager/model/*.java \
    "${SCRIPT_DIR}"/src/io/github/rianprei/bepinex/manager/core/*.java \
    "${SCRIPT_DIR}"/test/io/github/rianprei/bepinex/manager/test/*.java

echo "[*] Rodando TestRunner..."
java -cp "${BUILD_DIR}" io.github.rianprei.bepinex.manager.test.TestRunner

# --- (f) APK e keystore fora do git ---------------------------------------
# O build gera manager/bepinex-manager.apk, o .idsig e o .debug.keystore. Se
# algum deles entrar no indice um dia, o repo passa a carregar chave e APK
# assinado — e o .gitignore sozinho nao impede um `git add -f`.
if git -C "${SCRIPT_DIR}/.." rev-parse --git-dir >/dev/null 2>&1; then
    echo "[*] Conferindo que nenhum binario/segredo esta versionado..."
    TRACKED="$(git -C "${SCRIPT_DIR}/.." ls-files \
        | grep -iE '\.(apk|idsig|jks|keystore|p12|pfx|der)$' || true)"
    if [ -n "${TRACKED}" ]; then
        echo "ERRO: arquivo de APK/keystore versionado no git:" >&2
        echo "${TRACKED}" >&2
        echo "      Remova do indice (git rm --cached <arquivo>) e confira o .gitignore." >&2
        exit 1
    fi
    echo "[OK] Nenhum .apk/.idsig/keystore versionado."
else
    echo "[*] Sem repositorio git por perto: pulando a conferencia de binarios."
fi
