#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/test_classes"

mkdir -p "${BUILD_DIR}"

echo "[*] Compilando codigo e testes com javac --release 17..."
javac --release 17 -d "${BUILD_DIR}" \
    "${SCRIPT_DIR}"/src/io/github/rianprei/bepinex/manager/model/*.java \
    "${SCRIPT_DIR}"/src/io/github/rianprei/bepinex/manager/core/*.java \
    "${SCRIPT_DIR}"/test/io/github/rianprei/bepinex/manager/test/*.java

echo "[*] Rodando TestRunner..."
java -cp "${BUILD_DIR}" io.github.rianprei.bepinex.manager.test.TestRunner
