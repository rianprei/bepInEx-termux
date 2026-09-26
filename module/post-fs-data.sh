#!/system/bin/sh
# F1c — post-fs-data: cria a pasta de mods e aplica o tipo SELinux novo.
#
# ORDEM DE BOOT (importante, e documentada no KernelSU Module guide,
# https://kernelsu.org/guide/module.html ): o sepolicy.rule do modulo e aplicado
# ANTES dos scripts post-fs-data.sh do modulo, entao o tipo bepinex_mod_file ja
# existe quando este chcon roda. Se o chcon falhar com "invalid context", e o
# sepolicy.rule que nao carregou (magiskpolicy --print-rules | grep bepinex).
#
# Magisk/KernelSU executam este script no BusyBox ash standalone mode
# (https://topjohnwu.github.io/Magisk/guides.html), entao chcon/mkdir sao os
# applets do proprio busybox.

MODS=/data/local/tmp/mods
BC_MODS=/data/local/tmp/bc_mods
ALLOWLIST=/data/local/tmp/bc_generic_allowlist.conf

# Contrato C1: dono root, pasta 755. Sem isso o app (uid proprio) não consegue
# nem stat() o diretório.
mkdir -p "$MODS" "$BC_MODS"
chmod 755 "$MODS" "$BC_MODS"
chown 0:0 "$MODS" "$BC_MODS"

# Tipo novo em vez de shell_data_file: o app ganha acesso so aos .so de mod, e
# qualquer outro arquivo que o root largue em /data/local/tmp continua
# inacessivel pro jogo (ver module/sepolicy.rule).
#
# bc_mods entra pelo mesmo motivo (não quebrar o caminho Battle Cats): o loader
# faz dlopen de /data/local/tmp/bc_mods/*.so e lê os .conf de la, também de
# shell_data_file. Como a regra é por TIPO e não por caminho, rotular a arvore
# resolve sem nenhuma regra nova.
chcon -R u:object_r:bepinex_mod_file:s0 "$BC_MODS"

# Allowlist legada (experimento Cocos): arquivo opcional, então só rotula se já
# existir — sem isso o zygote nem access() nele consegue e o Cocos morre em
# Enforcing. Criado depois do boot pelo Manager/adb? Precisa de chcon de novo
# (o mesmo que o Manager faz ao instalar mod).
[ -f "$ALLOWLIST" ] && chcon u:object_r:bepinex_mod_file:s0 "$ALLOWLIST"

chcon -R u:object_r:bepinex_mod_file:s0 "$MODS" || {
    # Nesse ponto do boot o 'log' do toybox pode não existir ainda, e /cache
    # ainda não está montado. /data/adb já está, e o usuário vai ver o arquivo.
    echo "chcon falhou em $MODS: o sepolicy.rule nao aplicou?" >>/data/adb/bc-poc.log
    log -p t -t bepinex "chcon falhou em $MODS" 2>/dev/null
}
