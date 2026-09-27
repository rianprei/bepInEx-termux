#!/system/bin/sh
# Botão "Ação" do app Magisk (e do KernelSU): Magisk Developer Guides, árvore de
# módulos: "action.sh <--- This file will be executed when user click the action
# button in Magisk app" (https://topjohnwu.github.io/Magisk/guides.html).
# Roda em BusyBox ash standalone mode, como root, e a saída aparece no log do
# botão no app. Sem Manager instalado, é o diagnóstico na mão.
#
# MODDIR funciona aqui: este script é EXECUTADO (ao contrário do customize.sh,
# que é sourced), então $0 é o próprio caminho dele — é o que a doc manda usar.
MODDIR=${0%/*}
# Standalone mode usa os applets do busybox; pm/am/getenforce são do system.
PATH="$PATH:/system/bin"

MODS=/data/adb/bepinex/mods   # raiz root-only (ver post-fs-data.sh)
MANAGER=io.github.rianprei.bepinex.manager

# --- Manager instalado: abre ele e sai ----------------------------------------
if pm list packages 2>/dev/null | grep -q "^package:$MANAGER$"; then
    echo "Abrindo bepInEx Manager..."
    # Sem component: MAIN/LAUNCHER + -p resolvem a activity de launcher, e a
    # saida do am fica visível no log do botão (se falhar, o usuário vê por quê).
    am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p "$MANAGER" 2>&1
    exit 0
fi

# --- Sem Manager: status em texto ----------------------------------------------
echo "bepInEx-termux $(sed -n 's/^version=//p' "$MODDIR/module.prop" | head -n1) (id=$(sed -n 's/^id=//p' "$MODDIR/module.prop" | head -n1))"
echo "SELinux: $(getenforce 2>/dev/null || echo '?')"
if [ ! -d "$MODS" ]; then
    echo "Pasta de mods ausente: $MODS"
    echo "Nada a listar. Se instalou agora, reinicie o celular (o post-fs-data cria a pasta)."
    exit 0
fi
# -Z (contexto SELinux) é do toybox, não do busybox: caminho absoluto.
echo "Pasta: $MODS"
echo "Rotulo: $(/system/bin/ls -Zd "$MODS" 2>/dev/null || echo '(nao lido)')"

found=0
for d in "$MODS"/*/; do
    [ -d "$d" ] || continue
    found=1
    pkg=${d%/}
    pkg=${pkg##*/}
    echo ""
    echo "$pkg"
    on=""
    for f in "$d"*.so "$d"*.bpatch; do
        [ -e "$f" ] || continue
        on="$on ${f##*/}"
    done
    echo "  ativos:${on:- (nenhum)}"
    off=""
    for f in "$d"*.so.off "$d"*.bpatch.off; do
        [ -e "$f" ] || continue
        off="$off ${f##*/}"
    done
    [ -n "$off" ] && echo "  desligados:$off"
    log="/data/data/$pkg/files/bepinex/log.txt"
    if [ -f "$log" ]; then
        echo "  log (ultimas 5):"
        tail -n 5 "$log" | sed 's/^/    /'
    else
        echo "  log: (sem log.txt — o jogo rodou com o modulo?)"
    fi
done
[ "$found" = 0 ] && echo "" && echo "Nenhum jogo com pasta de mods em $MODS"
exit 0
