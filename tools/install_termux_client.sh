#!/usr/bin/env bash
# tools/install_termux_client.sh — coloca o cliente do REPL do Termux NO LUGAR
# que o console procura.
#
# ============================================================================
# POR QUE ISTO EXISTE (achado A4 do wiring-audit em 139aceb)
# ============================================================================
# 11 dos 12 verbos do companion (stream/ping/status/list_mods/list_patches/
# toggle_mod/set_mod/unpatch_mod/repatch_mod/hook_overhead/reload_config) tm
# como UNICO sender o REPL humano do Termux, e a cadeia quebrava fora da
# maquina do autor:
#
#   termux-console/bepin-console:18  CLIENT=~/battlecats-mods/zygisk-bc-poc/termux_client.py
#   jni/companion.cpp:1063           caminho fixo em
#                                   /data/data/com.getermux/files/home/battlecats-mods/...
#
# O cliente JA estava versionado em tools/termux_client.py — o que nao existia
# era NINGUEM colocando ele no home do Termux, que e um lugar diferente do repo.
# Num clone limpo, o console apontava para um arquivo que ninguem instalava.
#
# O QUE ISTO NAO FAZ: nao mexe em jni/companion.cpp nem em jni/main.cpp. O
# caminho fixo de companion.cpp:1063 e deste repo e esta no meio de outro
# trabalho (uni/mods-reloc mexe no companion). A nota sobre ele esta no
# relatorio do FEITO; aqui so o lado do Termux.
#
# O DESTINO e o home do Termux no aparelho. Fora do aparelho, o script
# instala no $HOME local, que e o mesmo caminho relativo — entao o mesmo
# comando serve para os dois casos e o teste de host exercita o de verdade.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

# O caminho que o bepin-console procura. TEM que ser o mesmo string do
# CLIENT=... em termux-console/bepin-console:18; se um mudar, o outro tem que
# mudar junto (o teste confere).
REL_DEST="battlecats-mods/zygisk-bc-poc/termux_client.py"
SRC="$REPO/tools/termux_client.py"

# Onde instalar. No aparelho e o home do Termux; fora dele, o $HOME de quem
# roda, que e a mesma arvore relativa.
DEST_ROOT="${1:-${HOME}}"
DEST="$DEST_ROOT/$REL_DEST"

[ -f "$SRC" ] || { echo "install_termux_client: fonte ausente: $SRC" >&2; exit 1; }

# ---------------------------------------------------------------------------
# NUNCA SEGUIR SYMLINK, em nenhum nivel (achado do hermes em 7d084a5)
# ---------------------------------------------------------------------------
# O hermes reproduziu: com um link plantado no caminho, o `cp` escrevia FORA da
# arvore de destino. Num aparelho isso e o root escrevendo onde um app mandou
# apontar — por isso a recusa e ANTES de qualquer escrita, e vale para o
# destino E para cada diretorio entre DEST_ROOT e ele.
#
# Nao basta checar o destino final: um `mods` symlink para /sdcard faz o
# caminho INTEIRO parecer legitimo e o cp grava em /sdcard. Por isso a checagem
# e de cada componente, do raiz ate o destino.
# ---------------------------------------------------------------------------
aviso() { echo "install_termux_client: $1" >&2; }

# 1) nenhum componente do caminho relativo pode ser symlink. Os diretorios
#    ainda podem nao existir: nesse caso nao ha link.
rel_dir="$(dirname "$REL_DEST")"
cur=""
for comp in $(echo "$rel_dir" | tr '/' ' '); do
    [ -n "$comp" ] || continue
    cur="$cur$comp"
    probe="$DEST_ROOT/$cur"
    if [ -L "$probe" ]; then
        aviso "recusado: $probe e symlink — escreveria fora da arvore de destino"
        exit 1
    fi
done
# 2) o DEST final tambem.
if [ -L "$DEST" ]; then
    aviso "recusado: o destino $DEST ja e symlink"
    exit 1
fi

mkdir -p "$(dirname "$DEST")" || { aviso "nao criei $(dirname "$DEST")"; exit 1; }
# 3) escrita ATOMICA: temporario no MESMO diretorio + mv. Um cp direto deixa o
#    arquivo pela metade se o processo morrer no meio, e o console executaria
#    um cliente truncado.
tmp="$(mktemp "$(dirname "$DEST")/.termux_client.XXXXXX")" || {
    aviso "mktemp falhou em $(dirname "$DEST")"; exit 1; }
# -p preserva o bit de execucao do fonte; o chmod abaixo normaliza.
cp -p "$SRC" "$tmp" || { rm -f "$tmp"; aviso "cp falhou"; exit 1; }
chmod 0755 "$tmp" || { rm -f "$tmp"; aviso "chmod falhou"; exit 1; }
mv -f "$tmp" "$DEST" || { rm -f "$tmp"; aviso "mv falhou para $DEST"; exit 1; }
printf '%s\n' "$DEST"
