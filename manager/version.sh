#!/usr/bin/env bash
# Le a versao do PROJETO do arquivo VERSION da raiz do repo e imprime
# "<versionCode> <versionName>" para o build usar.
#
# VERSION (raiz, uma linha): "<nome> <versionCode>"  ex.: "v0.4.1 401"
# O nome sai sem o "v" (0.4.1) porque e o versionName que o Android mostra.
#
# Uso: bash manager/version.sh [caminho-do-VERSION]
# Erro sai com codigo 1 e mensagem: builds nao podem inventar versao.
set -euo pipefail

VERSION_FILE="${1:-}"

if [ -z "${VERSION_FILE}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    VERSION_FILE="${SCRIPT_DIR}/../VERSION"
fi

if [ ! -f "${VERSION_FILE}" ]; then
    echo "ERRO: ${VERSION_FILE} ausente." >&2
    echo "      A versao do Manager vem do VERSION da raiz do repo (uma linha, ex: 'v0.4.1 401')." >&2
    exit 1
fi

LINE="$(head -n 1 "${VERSION_FILE}" | tr -d '\r')"
NAME="$(printf '%s' "${LINE}" | awk '{print $1}')"
CODE="$(printf '%s' "${LINE}" | awk '{print $2}')"

if ! printf '%s' "${NAME}" | grep -Eq '^v?[0-9]+(\.[0-9]+)*$' || ! printf '%s' "${CODE}" | grep -Eq '^[0-9]+$'; then
    echo "ERRO: VERSION invalido: '${LINE}'" >&2
    echo "      Formato esperado: '<nome> <versionCode>', ex: 'v0.4.1 401'." >&2
    exit 1
fi

CODE=$((10#${CODE}))
if [ "${CODE}" -le 0 ]; then
    echo "ERRO: versionCode tem que ser maior que zero: '${CODE}'." >&2
    exit 1
fi

printf '%s %s\n' "${CODE}" "${NAME#v}"
