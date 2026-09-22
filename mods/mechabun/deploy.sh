#!/usr/bin/env bash
# deploy.sh — envia libmechabun.so pro device via push_mod (protocolo do
# companion, socket abstract @bc_companion). NÃO É RODADO AUTOMATICAMENTE
# por nenhum agente/script desta sessão — script preparado, execução fica
# pra o usuário decidir e rodar manualmente quando quiser.
#
# Requer: device conectado via adb (adb devices), bepInEx-termux já
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

echo "Enviado. Confira com: adb shell logcat -d | grep -i mechabun"

# D16.1 — icons do deploy/upgrade (hook de fopen no mod redireciona pra
# cá). Push separado, opcional: sem esses 2 arquivos o hook so faz
# fallback pro fopen original (nao quebra nada, so os icons custom nao
# aparecem). NAO roda sozinho — só se o usuário confirmar de novo.
# Icons + scripts geradores consolidados em tools/ (junto do d12_transform.py).
ASSETS_DIR="$(dirname "$0")/tools"
ICON1="$ASSETS_DIR/uni426_s00.png"
ICON2="$ASSETS_DIR/udi426_s.png"
if [ -f "$ICON1" ] && [ -f "$ICON2" ]; then
    read -r -p "Tambem enviar os 2 icons (D16.1) pro path do redirect? (digite 'sim') " CONFIRM_ICONS
    if [ "$CONFIRM_ICONS" = "sim" ]; then
        adb shell "mkdir -p /data/local/tmp/bc_mods/mechabun_assets"
        adb push "$ICON1" "/data/local/tmp/bc_mods/mechabun_assets/uni426_s00.png"
        adb push "$ICON2" "/data/local/tmp/bc_mods/mechabun_assets/udi426_s.png"
        echo "Icons enviados."
    else
        echo "Icons nao enviados (opcional, mod funciona igual sem eles)."
    fi
fi

# D12 — pack de animacao (True Form attack, 426_s01, ciclo 32f->26f).
# Regenerado a cada deploy via tools/d12_transform.py a partir do pack
# original (nunca modificado) -- evita versionar/depender de um binario
# de 57MB que pode ficar stale. Sem o pack no device, o hook de fopen cai
# no fallback pro original (sem redirect, mod funciona igual sem D12).
# Overridable via env (repo é público; o default abaixo é a máquina do autor).
PACK_SRC_ORIGINAL="${PACK_SRC_ORIGINAL:-/home/rianprei/battlecats-mods/BCData/en_server/ImageDataServer_100600_00_en.pack}"
PACK_BUILD="$(dirname "$0")/build/ImageDataServer_100600_00_en.pack"
if [ -f "$PACK_SRC_ORIGINAL" ]; then
    read -r -p "Tambem gerar e enviar pack D12 (animacao True Form)? (digite 'sim') " CONFIRM_D12
    if [ "$CONFIRM_D12" = "sim" ]; then
        mkdir -p "$(dirname "$PACK_BUILD")"
        python3 "$(dirname "$0")/tools/d12_transform.py" "$PACK_BUILD"
        adb push "$PACK_BUILD" "/data/local/tmp/pack_d12_tmp"
        adb shell "su -c 'mv /data/local/tmp/pack_d12_tmp /data/local/tmp/bc_mods/mechabun_assets/ImageDataServer_100600_00_en.pack && chmod 666 /data/local/tmp/bc_mods/mechabun_assets/ImageDataServer_100600_00_en.pack'"
        echo "Pack D12 enviado."
    else
        echo "Pack D12 nao enviado (fallback do hook usa pack original, sem redirect)."
    fi
fi
