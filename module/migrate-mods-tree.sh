#!/system/bin/sh
# migrate-mods-tree.sh — muda a arvore de mods para um pai SÓ do root, uma vez.
#
# POR QUE MOVER (revisao de seguranca do freebuff, severidade ALTA):
# a arvore vivia sob /data/local/tmp, que e 0777 (o proprio companion faz
# chmod 0777 em jni/companion.cpp, e o AOSP cria 0771 shell:shell). Qualquer
# appuid no aparelho — incluindo o proprio jogo, e qualquer app que o usuario
# instale — pode criar e trocar /data/local/tmp/mods antes de o root tocar
# nele. Toda operacao de root nessa arvore (scandir, relabel, escrita de .conf)
# passa a poder ser desviada por um link simbolico.
#
# /data/local NAO serve: o AOSP tambem cria com 0771 shell:shell, ou seja, o
# shell ainda escreve nele. O unico pai que so o root altera e /data/adb
# (Magisk/KernelSU, 0700 root:root) — e por isso que /data/adb/bepinex/.
#
# POR QUE O JOGO NAO ABRE MAIS ESSE CAMINHO: com a arvore em /data/adb o
# processo do jogo nao tem (nem deve ter) acesso a ela. O companion (root)
# abre o .so e ENTREGA O FD pelo socket (SCM_RIGHTS), e o jogo abre com
# android_dlopen_ext(..., ANDROID_DLEXT_USE_LIBRARY_FD). Conf e allowlist
# vao por conteudo no mesmo socket. Ver jni/bc_mods_fd.h.
#
# Este arquivo e SOURCED pelo post-fs-data.sh, e tambem roda sozinho no teste
# de host (test/device/mods-reloc-test.sh) contra uma raiz falsa — por isso
# nada aqui depende de android, busybox ou Magisk.
#
# REGRA DURA DA MIGRAÇÃO: NUNCA seguir link simbolico. O destino da entrada é
# decidido por lstat(), e so arquivo regular (copia) ou diretorio real (move,
# recursivo com a mesma regra) sao migrados. Qualquer outra coisa — link
# simbolico, socket, fifo, device — FICA ONDE ESTA e e logada com o motivo. Um
# `mv` seguido de link quebraria a arvore e ainda assim reportaria sucesso.

# Raiz de mods. Fonte unica: BC_MODS_ROOT aqui e em jni/bc_loader.h
# (mesmo valor, mesmas duas palavras) e manager SuHelper.MODS_ROOT.
BEPINEX_ROOT=/data/adb/bepinex

# Raiz antiga (a que o shell/app podem manipular). Mantida aqui porque é
# history: some quando a migração terminar com sucesso.
BEPINEX_OLD_ROOT=/data/local/tmp

# _bep_migrate_um <entrada-antiga> <destino> <caminho-rel-log>
# Move UMA entrada da arvore antiga para a nova. Decide o tipo por lstat() e
# recusa seguir link. Devolve 0 em sucesso, 1 em "não migrou" (com motivo já
# logado). Quem chama decide se isso é erro fatal.
# sh NAO TEM `local`. A primeira versao desta funcao usava _src/_dst/_why e o
# proprio corpo sobrescrevia os tres — a recursao deixava _dst com o caminho do
# FILHO ANTERIOR, e a proxima iteracao calculava o destino do proximo arquivo a
# partir desse caminho. Resultado: so o primeiro arquivo de cada diretorio
# migrava, e o erro dizia "nao criei <destino do arquivo anterior>".
#
# Por isso os nomes sao separados por papel: `_e_*` e o do corpo da funcao
# (so ela mexe), `_l_*` e o do laco de quem chama (a funcao nunca mexe). Sem
# `local`, a separacao de nomes E a forma de ter variavel de cada quadro da
# recursao.
_bep_migrate_um() {
    _e_src="$1"
    _e_dst="$2"
    _e_why="$3"

    # lstat e NAO stat: e o stat que segue o link e mente sobre o tipo.
    # `ls -ld` e o unico lstat portavel em busybox/ash e em /bin/sh de host.
    if ! _e_st=$(ls -ld "$_e_src" 2>/dev/null); then
        echo "bepinex-migrate: nao consegui lstat de $_e_src — fica onde esta" >>"$_e_why"
        return 1
    fi

    # O primeiro caractere de `ls -l` e o tipo: - regular, d diretorio, l link,
    # p fifo, s socket, b/c dispositivo.
    case "$_e_st" in
        l*)
            # LINK (simbolico ou duro): mover criaria um link dentro da arvore
            # root-only, e um link de fora apontando pra dentro continuaria
            # valendo. Deixa e registra.
            echo "bepinex-migrate: $_e_src e link simbolico — NAO migrado (ficou onde esta)" >>"$_e_why"
            return 1
            ;;
        p*|s*|b*|c*)
            echo "bepinex-migrate: $_e_src e fifo/socket/dispositivo — NAO migrado" >>"$_e_why"
            return 1
            ;;
        d*)
            # Diretorio real: migra o conteudo recursivo, depois tenta remover o
            # vazio. A recursao reaplica a mesma regra em cada filho.
            mkdir -p "$_e_dst" 2>>"$_e_why" || {
                echo "bepinex-migrate: nao criei $_e_dst — $_e_src fica onde esta" >>"$_e_why"
                return 1
            }
            _e_rc=0
            # Os valores que o laco precisa sao copiados para _l_* ANTES do
            # laco. Nao e por estilo: a recursao reusa _e_src/_e_dst/_e_why
            # (sh nao tem `local`), entao a chamada filha SOBRESCREVE as
            # variaveis deste quadro. Sem esta copia, a segunda iteracao
            # calcula o destino do segundo arquivo a partir do caminho do
            # primeiro — e so o primeiro arquivo de cada diretorio migra.
            _l_base_dir="$_e_src"
            _l_target="$_e_dst"
            _l_why="$_e_why"
            for _l_child in "$_l_base_dir"/* "$_l_base_dir"/.[!.]* "$_l_base_dir"/..?*; do
                [ -e "$_l_child" ] || [ -L "$_l_child" ] || continue
                _l_base="${_l_child##*/}"
                _bep_migrate_um "$_l_child" "$_l_target/$_l_base" "$_l_why" || _e_rc=1
            done
            # rmdir so remove se estiver vazio; se sobrou algo (porque um link
            # recusou) ele recusha com ENOTEMPTY e o diretorio antigo fica de
            # pe, com o conteudo que nao pode ir. Usa a copia do laco porque
            # _e_src foi sobrescrito pela recursao.
            rmdir "$_l_base_dir" 2>/dev/null
            return $_e_rc
            ;;
        -*)
            # Arquivo regular: o unico tipo que migra.
            _l_dstdir="${_e_dst%/*}"
            [ "$_l_dstdir" = "$_e_dst" ] && _l_dstdir="."
            mkdir -p "$_l_dstdir" 2>>"$_e_why" || {
                echo "bepinex-migrate: nao criei $_l_dstdir — $_e_src fica onde esta" >>"$_e_why"
                return 1
            }
            if mv -f "$_e_src" "$_e_dst" 2>>"$_e_why"; then
                return 0
            fi
            echo "bepinex-migrate: mv falhou de $_e_src para $_e_dst" >>"$_e_why"
            return 1
            ;;
        *)
            echo "bepinex-migrate: tipo desconhecido em $_e_src — NAO migrado" >>"$_e_why"
            return 1
            ;;
    esac
}

# bep_migrate_tree <raiz-antiga> <raiz-nova> <log> <subpastas...>
# Cria a arvore nova com o dono certo e migra o que for seguro. Idempotente:
# rodar duas vezes com a arvore ja migrada nao faz nada e nao falha.
bep_migrate_tree() {
    _t_old="$1"; _t_new="$2"; _t_why="$3"; shift 3
    for _t_sub in "$@"; do
        [ -e "$_t_old/$_t_sub" ] || [ -L "$_t_old/$_t_sub" ] || continue
        if _bep_migrate_um "$_t_old/$_t_sub" "$_t_new/$_t_sub" "$_t_why"; then
            echo "bepinex-migrate: $_t_old/$_t_sub migrado" >>"$_t_why"
        else
            echo "bepinex-migrate: $_t_old/$_t_sub NAO migrado por completo; o que sobrou segue no lugar antigo" >>"$_t_why"
        fi
    done
    # Arquivos soltos na raiz antiga (bc_mods.conf, a allowlist).
    for _t_f in "$_t_old"/*.conf; do
        [ -e "$_t_f" ] || [ -L "$_t_f" ] || continue
        _t_base="${_t_f##*/}"
        if _bep_migrate_um "$_t_f" "$_t_new/$_t_base" "$_t_why"; then
            echo "bepinex-migrate: $_t_f migrado" >>"$_t_why"
        fi
    done
    return 0
}
