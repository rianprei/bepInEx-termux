#!/usr/bin/env bash
# deploy.sh — envia libmechabun.so pro device via push_mod (protocolo do
# companion, socket abstract @bc_companion). NÃO É RODADO AUTOMATICAMENTE
# por nenhum agente/script desta sessão — script preparado, execução fica
# pra o usuário decidir e rodar manualmente quando quiser.
#
# Requer: device conectado via adb (adb devices), bepinEx-termux já
# instalado e companion rodando (mesmo UID shell/root do dispositivo).
#
# Uso: ./deploy.sh
set -euo pipefail

SO="$(dirname "$0")/libs/arm64-v8a/libmechabun.so"
NAME="01_mechabun.so"  # prefixo numerico = ordem de carga (bc_loader.h)

if [ ! -f "$SO" ]; then
    echo "erro: $SO nao existe — rode 'ndk-build -B -j4' nesta pasta primeiro" >&2
    exit 1
fi

SIZE=$(stat -c%s "$SO")
echo "Vai enviar $SO ($SIZE bytes) como $NAME pro device via adb shell."
echo "Isso escreve em /data/local/tmp/bc_mods/ e sinaliza reload do loader."
read -r -p "Confirma? (digite 'sim' pra continuar) " CONFIRM
if [ "$CONFIRM" != "sim" ]; then
    echo "Cancelado."
    exit 1
fi

adb push "$SO" "/data/local/tmp/${NAME}.tmp"
adb shell "run-as $(adb shell getprop debug.bc_poc.termux_pkg 2>/dev/null || echo com.termux) \
    python3 -c \"
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('\\0bc_companion')
s.sendall(b'push_mod ${NAME} ${SIZE}\n')
with open('/data/local/tmp/${NAME}.tmp', 'rb') as f:
    s.sendall(f.read())
print(s.recv(256).decode())
\""

echo "Enviado. Confira com: adb shell logcat -d | grep -i mechabun"
