#!/system/bin/sh
# F1c — post-fs-data: cria a arvore de mods no /data/adb (pai root-only) e
# migra o que estiver na arvore antiga.
#
# ORDEM DE BOOT (importante, e documentada no KernelSU Module guide,
# https://kernelsu.org/guide/module.html ): o sepolicy.rule do modulo e aplicado
# ANTES dos scripts post-fs-data.sh do modulo. Magisk/KernelSU executam este
# script no BusyBox ash standalone mode
# (https://topjohnwu.github.io/Magisk/guides.html), entao chcon/mkdir sao os
# applets do proprio busybox.
#
# POR QUE /data/adb/bepinex E NAO /data/local/bepinex (revisao de seguranca do
# freebuff, severidade ALTA):
#   /data/local/tmp  — AOSP cria 0771 shell:shell e o proprio companion fazia
#                      chmod 0771->0777 (companion.cpp:1082, agora REMOVIDO).
#                      Qualquer appuid — inclusive o jogo — escreve nele. O
#                      AOSP ainda comenta "/data/local/tmp should always be
#                      empty": nao e area de estado de ninguem.
#   /data/local      — AOSP cria 0771 shell:shell tambem (init.rc), entao o
#                      shell ainda escreve. Nao serve.
#   /data/adb        — Magisk/KernelSU, root:root 0700. O SO ROOT altera.
# E o jogo deixa de precisar de acesso nenhum nessa arvore: o companion (root)
# abre o .so e entrega o FD pelo socket (SCM_RIGHTS), e o jogo abre com
# android_dlopen_ext(..., ANDROID_DLEXT_USE_LIBRARY_FD). Conf e allowlist vao
# por conteudo no mesmo socket. Ver jni/bc_mods_fd.h.

MODS_ROOT=/data/adb/bepinex
BC_MODS="$MODS_ROOT/bc_mods"
WHY=/data/adb/bepinex-migrate.log

# ---------------------------------------------------------------------------
# 0) REPARA O +x DO PROPRIO MODULO, antes de usar qualquer coisa.
#
# POR QUE ISTO AQUI E NAO SO NO customize.sh (medido no POCO C75, 2026-09-29,
# v0.5.0 e depois na build de integracao c4621ad0):
#
#   - o zip carrega 0755 e o git mode e 100755, mas magisk --install-module
#     (a CLI) EXTRAI tudo como 0644;
#   - o customize.sh tem set_perm para cada executavel, mas a CLI DELETA o
#     customize.sh sem executa-lo. So o flash pelo APP/Recovery roda a etapa
#     (util_functions.sh:703 unzip + :712 '. $MODPATH/customize.sh'). Medido:
#     apos instalar pela CLI, TODOS os .sh e o bepin-console ficaram 644.
#   - sem +x no post-fs-data.sh o Magisk nao o executa no boot. O magiskinit
#     faz exec (nao `sh arquivo`), logo nao ha como o proprio script corrigir
#     o que impede ele mesmo de rodar.
#
# Entao a garantia nao pode depender do instalador. Este bloco e o autorreparo:
# no primeiro boot em que o post-fs-data rodar (via app, recovery, ou chmod
# manual), ele deixa o resto do modulo executavel e, a partir dai, um boot
# normal restaura o modo sozinho. O efeito e idempotente e nao toca em nada
# fora de $MODDIR.
# ---------------------------------------------------------------------------
_moddir="${MODDIR:-/data/adb/modules/bc-poc}"
if [ -d "$_moddir" ]; then
    for _f in post-fs-data.sh action.sh uninstall.sh customize.sh \
              migrate-mods-tree.sh; do
        [ -f "$_moddir/$_f" ] && chmod 755 "$_moddir/$_f" 2>/dev/null
    done
    [ -f "$_moddir/termux-console/bepin-console" ] && \
        chmod 755 "$_moddir/termux-console/bepin-console" 2>/dev/null
    [ -f "$_moddir/termux-console/termux_client.py" ] && \
        chmod 644 "$_moddir/termux-console/termux_client.py" 2>/dev/null
    [ -f "$_moddir/sepolicy.rule" ] && chmod 644 "$_moddir/sepolicy.rule" 2>/dev/null
fi

# ---------------------------------------------------------------------------
# 1) Cria a arvore nova.
#
# 0755 e nao 0700 de proposito: o companion roda como root e o jogo nao entra
# por caminho nenhum, mas o `ls` do usuario via `su` e o Manager (que tambem
# usa su) precisam listar. O que barra o resto e o SELinux + o dono root do
# PAI (/data/adb 0700), nao o modo desta pasta.
# ---------------------------------------------------------------------------
mkdir -p "$MODS_ROOT" "$BC_MODS" "$MODS_ROOT/mods" 2>/dev/null
chown 0:0 "$MODS_ROOT" "$BC_MODS" "$MODS_ROOT/mods" 2>/dev/null
chmod 755 "$MODS_ROOT" "$BC_MODS" "$MODS_ROOT/mods" 2>/dev/null

# ---------------------------------------------------------------------------
# 2) MIGRA a arvore antiga, uma vez, sem seguir link simbolico.
#
# A logica esta em module/migrate-mods-tree.sh, que e SOURCED aqui e tambem
# roda sozinho no teste de host (test/device/mods-reloc-test.sh). Foi feito
# assim para o teste nao depender de android: a garantia critica ("um link no
# lugar do diretorio NAO e seguido") e shell puro.
# ---------------------------------------------------------------------------
# shellcheck disable=SC2034  # BEPINEX_ROOT e lido pelo script sourced abaixo,
# que o shellcheck nao atravessa. A allowlist tambem nao aparece aqui porque o
# chcon recursivo de _bep_chcon_safe cobre tudo que esta sob $MODS_ROOT.
BEPINEX_ROOT="$MODS_ROOT"
# shellcheck source=module/migrate-mods-tree.sh
# shellcheck disable=SC2034,SC1091
# ACHAR O MIGRADOR, sem depender do id do modulo. Medido no POCO C75
# (2026-09-29, base 04b93b3): o Magisk roda este script como post-fs-data do
# modulo, e $0 chega VAZIO nessa invocacao (o magiskinit executa o arquivo, nao
# passa o caminho como argv[0]). Com o $0 vazio, `dirname "$0"` devolve '.', o
# source procurou ./migrate-mods-tree.sh no cwd do magiskinit (/), nao achou, e
# a bep_migrate_tree nunca foi definida — a migracao rodou sem o log e sem
# erro visivel. Pior: o caminho fixo /data/adb/modules/bepinex-termux/ é de um
# id de modulo que NAO existe (o id deste e' bc-poc), entao ele nunca acertava.
#
# A busca agora e' por candidata realista, na ordem do mais provavel: o diretorio do
# proprio modulo (via MODDIR, que o Magisk exporta), dps o cwd, dps um
# `find` limitado sob /data/adb/modules. O script DEVE funcionar mesmo sem
# nenhum dos tres: nesse caso ele loga e segue com a arvore intacta, que e'
# melhor do que migrar pela metade em silencio.
_mig=""
for _cand in "$MODDIR/migrate-mods-tree.sh" \
             "$(dirname "${0:-/data/adb/modules/bc-poc}")/migrate-mods-tree.sh" \
             ./migrate-mods-tree.sh; do
    if [ -n "$_cand" ] && [ -f "$_cand" ]; then _mig="$_cand"; break; fi
done
if [ -z "$_mig" ]; then
    _mig=$(find /data/adb/modules -maxdepth 2 -name migrate-mods-tree.sh 2>/dev/null | head -1)
fi
if [ -n "$_mig" ]; then
    # shellcheck disable=SC1090  # o source e' DINAMICO por construcao: e'
    # justamente o que o bug B3 conserta. O caminho nao pode ser constante,
    # porque o id do modulo muda e o magiskinit deixa $0 vazio (medido no
    # POCO C75, 2026-09-29). O shellcheck nao consegue seguir um source que so
    # o runtime resolve — e o ponto e' que ele NAO e' constante.
    . "$_mig"
else
    echo "bepinex-migrate: migrate-mods-tree.sh nao encontrado (MODDIR=${MODDIR:-vazio} cwd=$(pwd)); arvore antiga intacta" >>"$WHY"
fi

if command -v bep_migrate_tree >/dev/null 2>&1; then
    bep_migrate_tree /data/local/tmp "$MODS_ROOT" "$WHY" mods bc_mods
else
    echo "bepinex-migrate: migrate-mods-tree.sh nao carregou; arvore antiga intacta" >>"$WHY"
fi

# ---------------------------------------------------------------------------
# 3) Rotula. Sem recursao em link simbolico: `chcon -R` segue link, e um link
#    apontando pra fora receberia o rotulo do tipo do mod. Cada entrada e
#    rotulada por lstat: so arquivo regular e diretorio real entram.
# ---------------------------------------------------------------------------
_bep_chcon_safe() {
    _c_target="$1"
    [ -e "$_c_target" ] || return 1
    # O primeiro caractere de `ls -ld` e o tipo do INODE (lstat, nao stat) —
    # e o que o migrador usa tambem. Shellcheck reclama de SC2012 porque o nome
    # pode ter caractere estranho, e o nome vem da arvore root-only, entao
    # isso e aceitavel aqui; a alternativa (find -printf) nao existe no busybox
    # do Magisk, que e onde este script roda.
    # shellcheck disable=SC2012
    _c_type=$(ls -ld "$_c_target" 2>/dev/null | cut -c1)
    case "$_c_type" in
        l*)
            echo "bepinex: $_c_target e link — nao rotulado" >>"$WHY"
            return 1
            ;;
        d)
            chcon u:object_r:bepinex_mod_file:s0 "$_c_target" >>"$WHY" 2>&1 || return 1
            for _c_child in "$_c_target"/* "$_c_target"/.[!.]* "$_c_target"/..?*; do
                [ -e "$_c_child" ] || [ -L "$_c_child" ] || continue
                _bep_chcon_safe "$_c_child"
            done
            return 0
            ;;
        *)
            chcon u:object_r:bepinex_mod_file:s0 "$_c_target" >>"$WHY" 2>&1
            return $?
            ;;
    esac
}

_bep_chcon_safe "$MODS_ROOT" || {
    # Nesse ponto do boot o 'log' do toybox pode nao existir ainda, e /cache ainda
    # nao esta montado. /data/adb ja esta, e o usuario vai ver o arquivo.
    echo "chcon falhou em $MODS_ROOT: o sepolicy.rule nao aplicou?" >>"$WHY"
    log -p t -t bepinex "chcon falhou em $MODS_ROOT" 2>/dev/null
}
