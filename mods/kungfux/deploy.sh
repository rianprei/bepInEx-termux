#!/usr/bin/env bash
# deploy.sh — envia libkungfux.so pro device via push_mod (protocolo do
# companion, socket abstract @bc_companion). NÃO É RODADO AUTOMATICAMENTE
# por nenhum agente/script desta sessão — script preparado, execução fica
# pra o usuário decidir e rodar manualmente quando quiser.
#
# Requer: device conectado via adb (adb devices), bepInEx-termux já
# instalado e companion rodando (mesmo UID shell/root do dispositivo), e
# PYTHON do Termux no aparelho: o emissor do push_mod roda LÁ, e o Termux de
# base não traz python. Sem ele: `pkg install python` dentro do Termux.
#
# Uso: ./deploy.sh
set -euo pipefail

SO="$(dirname "$0")/libs/arm64-v8a/libkungfux.so"
NAME="02_kungfux.so"  # prefixo numerico = ordem de carga (bc_loader.h)

if [ ! -f "$SO" ]; then
    echo "erro: $SO nao existe — rode 'ndk-build -B -j4' nesta pasta primeiro" >&2
    exit 1
fi

# O .so do build sai NÃO-stripado (repro.mk); o device só recebe stripado.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$SCRIPT_DIR/../../tools/symbols.sh"
STAGED="$(mktemp "/tmp/${NAME}.XXXXXX.so")"
trap 'rm -f "$STAGED"' EXIT
symbols_ship "$SO" "$STAGED" || exit 1
SO="$STAGED"

TERMUX_PY="${TERMUX_PY:-/data/data/com.termux/files/usr/bin/python3}"

# stat -c%s e' GNU (Linux); -f%z e' BSD/macOS -- tenta os dois.
SIZE=$(stat -c%s "$SO" 2>/dev/null || stat -f%z "$SO")
echo "Vai enviar $SO ($SIZE bytes) como $NAME pro device via adb shell."
echo "Isso escreve em /data/local/tmp/bc_mods/ e sinaliza reload do loader."
# O emissor do push_mod roda NO APARELHO, com o python do Termux, e o Termux
# de base nao instala python. Sem este preflight a falha aparece no meio do
# fluxo, com o .so e o emissor ja empurrados, e a mensagem e a do shell do
# aparelho. Aqui ela vem antes de enviar qualquer coisa, e diz o comando.
# Checar antes do prompt: nao faz o usuario confirmar um deploy que nao vai
# funcionar, e nada foi enviado, entao nao ha residuo para limpar.
if ! adb shell "test -x '$TERMUX_PY'" >/dev/null 2>&1; then
    echo "erro: python do Termux ausente no aparelho ($TERMUX_PY)." >&2
    echo "      o emissor do push_mod roda la dentro; sem python o deploy nao" >&2
    echo "      tem como concluir. No Termux do aparelho:" >&2
    echo "        pkg install python" >&2
    echo "      (ou aponte TERMUX_PY para um python3 ja existente)" >&2
    exit 1
fi

read -r -p "Confirma? (digite 'sim' pra continuar) " CONFIRM
if [ "$CONFIRM" != "sim" ]; then
    echo "Cancelado."
    exit 1
fi

adb push "$SO" "/data/local/tmp/${NAME}.tmp"
# O emissor é o tools/push_mod_emit.py versionado (a mesma fonte que o gate
# testa no host): lê o .tmp UMA vez e só anuncia/envia se o tamanho bater
# com o SIZE medido aqui — SIZE anunciado == bytes enviados por construção.
adb push "$SCRIPT_DIR/../../tools/push_mod_emit.py" "/data/local/tmp/push_mod_emit.py"
# achado de review: antes nao validava a resposta do companion -- "Enviado"
# aparecia mesmo se o companion respondesse "error: ..." (exit code da adb
# shell continua 0, so imprime o texto). Agora falha visivelmente se a
# resposta nao contiver "ok".
RESPONSE="$(adb shell su -c "$TERMUX_PY /data/local/tmp/push_mod_emit.py @bc_companion /data/local/tmp/${NAME}.tmp '$NAME' '$SIZE'" < /dev/null)"
echo "$RESPONSE"
# achado de review: push_mod_emit.py e o .tmp do .so ficavam residuais em
# /data/local/tmp/ apos todo deploy -- limpa do lado do device tambem.
adb shell "su -c 'rm -f /data/local/tmp/push_mod_emit.py /data/local/tmp/${NAME}.tmp'" || true
if ! echo "$RESPONSE" | grep -qi "ok"; then
    echo "erro: companion nao confirmou sucesso (resposta acima) — mod pode nao ter sido adotado" >&2
    exit 1
fi

echo "Enviado. Confira com: adb shell logcat -d | grep -i kungfux"
