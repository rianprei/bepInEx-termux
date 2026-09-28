#!/usr/bin/env bash
# deploy.sh — envia libmechabun.so pro device via push_mod (protocolo do
# companion, socket abstract @bc_companion). NÃO É RODADO AUTOMATICAMENTE
# por nenhum agente/script desta sessão — script preparado, execução fica
# pra o usuário decidir e rodar manualmente quando quiser.
#
# Requer: device conectado via adb (adb devices), root NO APARELHO via su
# (Magisk/KernelSU — `adb root` não serve: adbd de produção recusa root),
# bepInEx-termux já instalado e companion rodando, e
# PYTHON do Termux no aparelho: o emissor do push_mod roda LÁ, e o Termux de
# base não traz python. Sem ele: `pkg install python` dentro do Termux.
#
# Uso: ./deploy.sh
set -euo pipefail

SO="$(dirname "$0")/libs/arm64-v8a/libmechabun.so"
NAME="01_mechabun.so"  # prefixo numerico = ordem de carga (bc_loader.h)

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
# O caminho vai dentro de `su -c "..."` duas vezes: no preflight e no envio.
# Valida-lo aqui, local, antes de qualquer adb, fecha os dois sítios de uma vez:
# aspas, espaço ou `;` virariam comando no aparelho, como root. O padrão
# aceita o que o caminho do Termux tem de fato e nada mais.
case "$TERMUX_PY" in
    /*) ;;
    *) echo "erro: TERMUX_PY tem que ser caminho absoluto: $TERMUX_PY" >&2; exit 1 ;;
esac
printf '%s' "$TERMUX_PY" | grep -qE '^/[A-Za-z0-9._/+-]+$' || {
    echo "erro: TERMUX_PY tem caractere fora de [A-Za-z0-9._/+-]: $TERMUX_PY" >&2
    echo "      o caminho entra num su -c como root; aspas, espaco ou ; viram comando." >&2
    exit 1
}

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
# O caminho tem que ser conferido como root e no MESMO contexto do envio, que
# tambem e `su -c`. Sem su, o uid do adb (2000) nao atravessa
# /data/data/com.termux, que e 0700 do app do Termux: o `test -x` dava
# "ausente" em TODO aparelho real e abortava um deploy que funcionaria. Por
# isso a checagem e `su -c`, e a falha de root tem mensagem propria.
# `adb root` NAO e consultado, de proposito. Em build de producao o adbd recusa
# root ("adbd cannot run as root in production builds"), entao perguntar aborta o
# deploy ANTES de tentar o su -- que e justamente o caminho que funciona em
# aparelho com Magisk/KernelSU. O deploy nao precisa de root do adb: precisa de
# root no aparelho, e a pergunta honesta e `su -c id` respondendo uid=0.
#
# A captura vem antes do `case` e nao num pipe: `adb shell ... | grep -q` sob
# `set -o pipefail` e uma corrida (o grep sai quando casa, o writer leva
# SIGPIPE, o pipeline vira 141 com o root presente), e o resultado seria
# abortar o deploy num aparelho que tem root.
ROOT_ID=$(adb shell su -c id 2>/dev/null || true)
case "$ROOT_ID" in
    *uid=0*) ;;
    *)
        echo "erro: root necessario: o aparelho precisa de su (Magisk/KernelSU)." >&2
        echo "      este deploy escreve em /data/local/tmp/bc_mods/ e le o" >&2
        echo "      python do Termux por 'su -c'. root de adb nao e o caminho:" >&2
        echo "      em build de producao o adbd recusa ('adbd cannot run as root" >&2
        echo "      in production builds'). Instale Magisk/KernelSU, ou faca o" >&2
        echo "      deploy num aparelho com root de adb." >&2
        exit 1
        ;;
esac
if ! adb shell su -c "test -x '$TERMUX_PY'" >/dev/null 2>&1; then
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
# Sem pipe pelo mesmo motivo do ROOT_ID: a resposta do companion decide se o
# deploy deu certo, e um 141 de SIGPIPE aqui vira "deu errado" num aparelho que
# deu certo. Busca de texto puro, sem processo no meio.
case "$RESPONSE" in
    *[Oo][Kk]*) ;;
    *)
        echo "erro: companion nao confirmou sucesso (resposta acima) — mod pode nao ter sido adotado" >&2
        exit 1
        ;;
esac

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
# achado de review (hermes): default anterior era o path pessoal do autor
# hardcoded -- repo é público, sem sentido vazar estrutura de disco de quem
# manteve o projeto. Sem a env setada, PACK_SRC_ORIGINAL fica vazio e o
# passo abaixo é pulado de forma limpa (mesmo comportamento de "arquivo
# ausente" que já existia).
PACK_SRC_ORIGINAL="${PACK_SRC_ORIGINAL:-}"
PACK_BUILD="$(dirname "$0")/build/ImageDataServer_100600_00_en.pack"
if [ -n "$PACK_SRC_ORIGINAL" ] && [ -f "$PACK_SRC_ORIGINAL" ]; then
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
