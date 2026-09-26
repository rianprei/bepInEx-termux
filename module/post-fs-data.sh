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

# Contrato C1: dono root, pasta 755. Sem isso o app (uid proprio) não consegue
# nem stat() o diretório.
mkdir -p "$MODS"
chmod 755 "$MODS"
chown 0:0 "$MODS"

# Tipo novo em vez de shell_data_file: o app ganha acesso so aos .so de mod, e
# qualquer outro arquivo que o root largue em /data/local/tmp continua
# inacessivel pro jogo (ver module/sepolicy.rule).
chcon -R u:object_r:bepinex_mod_file:s0 "$MODS" ||
    log -p t -t bepinex "chcon falhou em $MODS: sepolicy.rule não aplicou?"
