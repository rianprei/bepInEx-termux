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

# O console agora mora NO MÓDULO (companion lança de lá, com check de
# dono/modo; o home pessoal saiu da cadeia). O bepin-console acha o cliente
# BESIDE SI MESMO: MODULE_DIR=${0%/*}; CLIENT="$MODULE_DIR/termux_client.py".
# Este instalador espelha o MESMO layout do stage do módulo
# (build_module.sh: termux-console/bepin-console + termux_client.py) — no
# aparelho quem stages é o zip do módulo; fora dele, este script reproduz a
# mesma árvore para o teste de host exercitar o cliente de verdade.
REL_DEST="termux-console/termux_client.py"

# O emissor e ARTEFATO DO MESMO INSTALL, nao um extra opcional (achado do
# hermes): o cliente importava uma copia propria da logica de push_mod, que
# e a mesma que o push_mod_emit.py oficial ja faz. Com a copia removida, o
# cliente depende do emissor — entao instalar so o cliente produz um cliente
# que nao roda. Os dois vao juntos, lado a lado, porque o cliente importa o
# emissor do SEU proprio diretorio (sys.path com o dir do arquivo), que e o
# mesmo caminho onde o console procura o cliente.
REL_DEST_EMIT="termux-console/push_mod_emit.py"
SRC="$REPO/tools/termux_client.py"
SRC_EMIT="$REPO/tools/push_mod_emit.py"

# Onde instalar. No aparelho e o home do Termux; fora dele, o $HOME de quem
# roda, que e a mesma arvore relativa.
DEST_ROOT="${1:-${HOME}}"
DEST="$DEST_ROOT/$REL_DEST"
DEST_EMIT="$DEST_ROOT/$REL_DEST_EMIT"

[ -f "$SRC" ] || { echo "install_termux_client: fonte ausente: $SRC" >&2; exit 1; }
[ -f "$SRC_EMIT" ] || { echo "install_termux_client: fonte ausente: $SRC_EMIT" >&2; exit 1; }

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
#
# A checagem e por COMPONENTE DO CAMINHO, e os dois arquivos moram no mesmo
# diretorio, entao a varredura e a mesma para os dois — nao ha como o cliente
# passar e o emissor falhar, nem o inverso.
# ---------------------------------------------------------------------------
aviso() { echo "install_termux_client: $1" >&2; }

# 1) nenhum componente do caminho relativo pode ser symlink. Os diretorios
#    ainda podem nao existir: nesse caso nao ha link.
for rel_dir in "$(dirname "$REL_DEST")" "$(dirname "$REL_DEST_EMIT")"; do
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
done
# 2) os DESTinos finais tambem. Os dois: um deles e symlink e escrever nele
#    e o mesmo perigo do link no meio do caminho.
for dest in "$DEST" "$DEST_EMIT"; do
    if [ -L "$dest" ]; then
        aviso "recusado: o destino $dest ja e symlink"
        exit 1
    fi
done

mkdir -p "$(dirname "$DEST")" || { aviso "nao criei $(dirname "$DEST")"; exit 1; }

# 3) escrita ATOMICA: temporario no MESMO diretorio + mv. Um cp direto deixa o
#    arquivo pela metade se o processo morrer no meio, e o console executaria
#    um cliente truncado.
# O temporario e por arquivo, e a escrita so é declarada boa no fim: se o
# segundo falhar, o primeiro ja esta no lugar e o dest da segunda esta
# intacto, entao nao sobra metade de um install em estado impossivel de
# diagnosticar.
instalar() {
    local src="$1" dest="$2" tmp
    tmp="$(mktemp "$(dirname "$dest")/.$(basename "$dest").XXXXXX")" || {
        aviso "mktemp falhou em $(dirname "$dest")"; return 1; }
    # -p preserva o bit de execucao do fonte; o chmod abaixo normaliza.
    cp -p "$src" "$tmp" || { rm -f "$tmp"; aviso "cp falhou para $dest"; return 1; }
    chmod 0755 "$tmp" || { rm -f "$tmp"; aviso "chmod falhou para $dest"; return 1; }
    mv -f "$tmp" "$dest" || { rm -f "$tmp"; aviso "mv falhou para $dest"; return 1; }
}

instalar "$SRC" "$DEST" || exit 1
instalar "$SRC_EMIT" "$DEST_EMIT" || exit 1
printf '%s\n%s\n' "$DEST" "$DEST_EMIT"
