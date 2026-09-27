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
echo "  /data/adb/bepinex/mods/     mods .so/.conf/.bpatch por pacote (contrato C1)"
echo "  /data/adb/bepinex/bc_mods/  mods do caminho Battle Cats"
echo "  /data/adb/bepinex/bc_generic_allowlist.conf  lista do experimento Cocos"
echo ""
echo "A arvore mora em /data/adb/bepinex (root:root 0700) porque /data/local/tmp"
echo "e 0777 — qualquer appuid do aparelho escrevia nela. O processo do jogo nao"
echo "abre esse caminho: o companion entrega o FD do .so pelo socket."
echo ""
echo "Para apagar de vez: rm -rf /data/adb/bepinex"
echo "Sobra do caminho antigo, se alguma entrada nao migrou (link simbolico,"
echo "fifo): /data/local/tmp/mods e /data/local/tmp/bc_mods — veja"
echo "/data/adb/bepinex-migrate.log."
