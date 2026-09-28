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
RUNNER_LOG="$(mktemp)"
trap 'rm -f "${RUNNER_LOG}"' EXIT
if java -cp "${BUILD_DIR}" io.github.rianprei.bepinex.manager.test.TestRunner 2>&1 \
    | tee "${RUNNER_LOG}"; then
    RUNNER_STATUS=0
else
    RUNNER_STATUS=$?
fi

SUMMARY_COUNT="$(grep -Ec '^RUNNER: checks=[0-9]+ falhas=[0-9]+$' "${RUNNER_LOG}" || true)"
if [ "${SUMMARY_COUNT}" -ne 1 ]; then
    echo "ERRO: resumo RUNNER ausente ou duplicado" >&2
    exit 1
fi
SUMMARY="$(grep -E '^RUNNER: checks=[0-9]+ falhas=[0-9]+$' "${RUNNER_LOG}")"
CHECKS="$(printf '%s\n' "${SUMMARY}" | sed -E 's/^RUNNER: checks=([0-9]+) falhas=[0-9]+$/\1/')"
FAILURES="$(printf '%s\n' "${SUMMARY}" | sed -E 's/^RUNNER: checks=[0-9]+ falhas=([0-9]+)$/\1/')"
# Baseline vem de manager/test_checks_baseline, nao de um numero escrito aqui:
# a comparacao e por IGUALDADE, entao suite que cresce ou encolhe sem
# atualizar o arquivo no mesmo commit quebra o gate em vez de passar calada.
BASELINE_FILE="${SCRIPT_DIR}/test_checks_baseline"
if [ ! -f "${BASELINE_FILE}" ]; then
    echo "ERRO: ${BASELINE_FILE} ausente: sem baseline nao ha como validar a suite" >&2
    exit 1
fi
MIN_CHECKS="$(grep -Eo '[0-9]+' "${BASELINE_FILE}" | head -1)"
if [ -z "${MIN_CHECKS}" ]; then
    echo "ERRO: ${BASELINE_FILE} sem numero de checks" >&2
    exit 1
fi

if grep -Fq '[RUNNER FAIL]' "${RUNNER_LOG}"; then
    echo "ERRO: TestRunner registrou falha por teste" >&2
    exit 1
fi
if [ "${FAILURES}" -ne 0 ]; then
    echo "ERRO: TestRunner reportou ${FAILURES} falha(s)" >&2
    exit 1
fi
if [ "${CHECKS}" -ne "${MIN_CHECKS}" ]; then
    echo "ERRO: TestRunner executou ${CHECKS} checks; baseline ${MIN_CHECKS}. Atualize manager/test_checks_baseline no MESMO commit do teste novo (ou do teste removido)." >&2
    exit 1
fi
if [ "${RUNNER_STATUS}" -ne 0 ]; then
    echo "ERRO: TestRunner terminou com status ${RUNNER_STATUS}" >&2
    exit 1
fi

# --- (f) APK e keystore fora do git ---------------------------------------
# O build gera manager/bepinex-manager.apk, o .idsig e o .debug.keystore. Se
# algum deles entrar no indice um dia, o repo passa a carregar chave e APK
# assinado — e o .gitignore sozinho nao impede um `git add -f`.
if git -C "${SCRIPT_DIR}/.." rev-parse --git-dir >/dev/null 2>&1; then
    echo "[*] Conferindo que nenhum binario/segredo esta versionado..."
    TRACKED="$(git -C "${SCRIPT_DIR}/.." ls-files \
        | grep -iE '\.(apk|idsig|jks|keystore|p12|pfx|der)$' \
        | grep -v '^test/fixtures/' || true)"
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
