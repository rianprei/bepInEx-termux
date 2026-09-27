#!/system/bin/sh
# Roda quando o usuário (ou o app Magisk) remove o módulo.
#
# NÃO apaga nada do usuário: os mods instalados e os logs do jogo continuam
# onde estão, e um "rm -rf" aqui perderia trabalho de graça na hora de uma
# desinstalação acidental. O que sobra é listado para o usuário decidir.
#
# MODDIR funciona aqui (ao contrário do customize.sh) porque este script é
# EXECUTADO, não sourced — $0 é o próprio caminho dele.
echo "bepInEx-termux removido (o Zygisk para de carregar no proximo boot)."
echo "Seus arquivos continuam no device:"
echo "  /data/local/tmp/mods/           mods .so/.conf/.bpatch por pacote (contrato C1)"
echo "  /data/local/tmp/bc_mods/        mods do caminho Battle Cats"
echo "  /data/local/tmp/bc_generic_allowlist.conf  lista do experimento Cocos"
echo "  /data/data/<pacote>/files/bepinex/         log.txt e dump.tsv de cada jogo"
echo "Para apagar de vez: rm -rf /data/local/tmp/mods /data/local/tmp/bc_mods"
