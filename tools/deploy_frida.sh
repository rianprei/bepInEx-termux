#!/bin/sh
# Instala mod Frida .js (+ gadget + config) em mods/<pkg>/ via adb+su.
# Uso: tools/deploy_frida.sh <pkg> <mod.js> [...]
# Faz o papel de "quem instala" (Manager faz o mesmo via su): copia .js e
# frida-gadget.bin, ESCREVE frida-gadget.config (o jogo só verifica — nunca
# escreve na pasta de mods), tudo com chmod 644 + chcon bepinex_mod_file.
# Sem device? Só revise com sh -n (agente não usa adb).
set -eu
PKG=${1:?uso: tools/deploy_frida.sh <pkg> <mod.js> [...]}
shift
[ $# -ge 1 ] || { echo "informe ao menos um .js"; exit 1; }
GADGET_BIN="mods/u_frida/gadget/frida-gadget.bin"
[ -f "$GADGET_BIN" ] || { echo "rode tools/fetch_frida_gadget.sh antes"; exit 1; }
MODS="/data/local/tmp/mods/$PKG"
STAGE="/data/local/tmp/frida-stage-$PKG"

dev() { printf '%s\n' "$1" | adb shell su; }

dev "rm -rf $STAGE && mkdir -p $STAGE && chown 2000:2000 $STAGE && chmod 775 $STAGE"
for f in "$@"; do
    [ -f "$f" ] || { echo "ausente: $f"; exit 1; }
    adb push "$f" "$STAGE/$(basename "$f")" >/dev/null
done
adb push "$GADGET_BIN" "$STAGE/frida-gadget.bin" >/dev/null
dev "cp $STAGE/* $MODS/ && chmod 644 $MODS/*.js $MODS/frida-gadget.bin"
dev "chcon u:object_r:bepinex_mod_file:s0 $MODS/*.js $MODS/frida-gadget.bin" 2>/dev/null \
    || echo "AVISO: chcon falhou (tipo inexistente? Enforcing pode negar)"
# Mesmo JSON que uf_build_config() gera (modo script-directory pra pasta).
JSON="{\"interaction\":{\"type\":\"script-directory\",\"path\":\"$MODS\",\"on_change\":\"ignore\"}}"
dev "printf '%s' '$JSON' > $MODS/frida-gadget.config && chmod 644 $MODS/frida-gadget.config"
dev "chcon u:object_r:bepinex_mod_file:s0 $MODS/frida-gadget.config" 2>/dev/null \
    || echo "AVISO: chcon falhou no .config"
dev "rm -rf $STAGE"
echo "instalado em $MODS (reinicie o jogo)"
