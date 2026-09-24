#!/usr/bin/env bash
# deploy.sh — envia libkungfux.so pro device via push_mod (protocolo do
# companion, socket abstract @bc_companion). NÃO É RODADO AUTOMATICAMENTE
# por nenhum agente/script desta sessão — script preparado, execução fica
# pra o usuário decidir e rodar manualmente quando quiser.
#
# Requer: device conectado via adb (adb devices), bepInEx-termux já
# instalado e companion rodando (mesmo UID shell/root do dispositivo).
#
# Uso: ./deploy.sh
set -euo pipefail

SO="$(dirname "$0")/libs/arm64-v8a/libkungfux.so"
NAME="02_kungfux.so"  # prefixo numerico = ordem de carga (bc_loader.h)

if [ ! -f "$SO" ]; then
    echo "erro: $SO nao existe — rode 'ndk-build -B -j4' nesta pasta primeiro" >&2
    exit 1
fi

# stat -c%s e' GNU (Linux); -f%z e' BSD/macOS -- tenta os dois.
SIZE=$(stat -c%s "$SO" 2>/dev/null || stat -f%z "$SO")
echo "Vai enviar $SO ($SIZE bytes) como $NAME pro device via adb shell."
echo "Isso escreve em /data/local/tmp/bc_mods/ e sinaliza reload do loader."
read -r -p "Confirma? (digite 'sim' pra continuar) " CONFIRM
if [ "$CONFIRM" != "sim" ]; then
    echo "Cancelado."
    exit 1
fi

adb push "$SO" "/data/local/tmp/${NAME}.tmp"
# achado de review: hardcoded, sem override -- path padrao do Termux, mas
# pode divergir por instalacao/variante (F-Droid vs Play Store).
TERMUX_PY="${TERMUX_PY:-/data/data/com.termux/files/usr/bin/python3}"
PUSH_SCRIPT="$(mktemp)"
cat > "$PUSH_SCRIPT" <<EOF
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('\0bc_companion')
s.sendall(b'push_mod ${NAME} ${SIZE}\n')
with open('/data/local/tmp/${NAME}.tmp', 'rb') as f:
    s.sendall(f.read())
print(s.recv(256).decode())
EOF
adb push "$PUSH_SCRIPT" "/data/local/tmp/push_mod.py"
rm -f "$PUSH_SCRIPT"
# achado de review: antes nao validava a resposta do companion -- "Enviado"
# aparecia mesmo se o companion respondesse "error: ..." (exit code da adb
# shell continua 0, so imprime o texto). Agora falha visivelmente se a
# resposta nao contiver "ok".
RESPONSE="$(adb shell su -c "$TERMUX_PY /data/local/tmp/push_mod.py" < /dev/null)"
echo "$RESPONSE"
# achado de review: push_mod.py e o .tmp do .so ficavam residuais em
# /data/local/tmp/ apos todo deploy -- limpa do lado do device tambem.
adb shell "su -c 'rm -f /data/local/tmp/push_mod.py /data/local/tmp/${NAME}.tmp'" || true
if ! echo "$RESPONSE" | grep -qi "ok"; then
    echo "erro: companion nao confirmou sucesso (resposta acima) — mod pode nao ter sido adotado" >&2
    exit 1
fi

echo "Enviado. Confira com: adb shell logcat -d | grep -i kungfux"
