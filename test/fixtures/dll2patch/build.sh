#!/usr/bin/env bash
# Build determinístico da fixture dll2patch. Regenera as .dll de teste.
# Uso: bash test/fixtures/dll2patch/build.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}/src"

# Limpa build anterior para garantir determinismo
rm -rf bin obj

# Compila com determinismo total
dotnet build -c Release -p:Deterministic=true -p:ContinuousIntegrationBuild=true --nologo -v quiet

# Copia a DLL gerada para o diretório pai (onde o teste espera)
cp bin/Release/net6.0/dll2patch_fixture.dll "${SCRIPT_DIR}/../dll2patch_fixture.dll"

echo "Fixture compilada: ${SCRIPT_DIR}/../dll2patch_fixture.dll"
