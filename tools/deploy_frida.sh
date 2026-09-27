#!/bin/sh
# Instala mod Frida .js (+ gadget + config) em mods/<pkg>/ via adb+su.
# Uso: tools/deploy_frida.sh <pkg> <mod.js> [...]
# Hoje é o ÚNICO instalador do u_frida (o Manager/F5 ainda não tem código
# frida): copia .js e frida-gadget.bin, ESCREVE frida-gadget.config (o jogo
# só verifica — nunca escreve na pasta de mods), tudo com chmod 644 +
# chcon bepinex_mod_file. Um arquivo por vez, exit conferido em cada passo:
# qualquer falha aborta mostrando o erro, "instalado" só se tudo deu 0.
# Sem device? Só revise com sh -n (agente não usa adb).
set -eu

die() { echo "ERRO: $*" >&2; exit 1; }

PKG=${1:?uso: tools/deploy_frida.sh <pkg> <mod.js> [...]}
shift
# $PKG vira caminho e entra em comando root: valida ANTES de qualquer adb
# (mesma regra do uf_pkg_ok() em mods/u_frida/jni/u_frida_config.h).
case "$PKG" in
    ''|.*|*..*|zygote*|*[!a-zA-Z0-9._]*) die "pacote inválido: '$PKG' (só [A-Za-z0-9._], sem '..')" ;;
esac
[ $# -ge 1 ] || die "informe ao menos um .js"
NL='
'
for f in "$@"; do
    [ -f "$f" ] || die "ausente: $f"
    b=$(basename "$f")
    # Mesma regra do uf_is_js_mod(): *.js, não oculto, sem quebra de linha.
    case "$b" in
        .*|*"$NL"*) die "nome de .js inválido: '$b'" ;;
        *.js) ;;
        *) die "não é .js: '$b'" ;;
    esac
done
GADGET_BIN="mods/u_frida/gadget/frida-gadget.bin"
[ -f "$GADGET_BIN" ] || die "rode tools/fetch_frida_gadget.sh antes"
MODS="/data/adb/bepinex/mods/$PKG"   # arvore root-only (ver post-fs-data.sh). O adb nao le /data/adb: o push vai para /data/local/tmp e um su -c mv coloca no lugar.
STAGE="/data/local/tmp/frida-stage-$PKG"
CTX="u:object_r:bepinex_mod_file:s0"

# Aspa simples pro shell root do device (nome de .js pode ter espaço/aspa).
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

# Roda $1 como root no device. Sucesso só se o comando remoto saiu 0: o rc
# volta por sentinela no stdout (adb sem shell protocol não repassa o exit
# remoto). Saída/erro remoto sempre aparecem.
dev() {
    out=$(printf '%s\n' "$1; echo \"__rc=\$?\"" | adb shell su 2>&1 | tr -d '\r') || true
    printf '%s\n' "$out" | grep -v '^__rc=' >&2 || true
    [ "$(printf '%s\n' "$out" | sed -n 's/^__rc=//p' | tail -n 1)" = 0 ]
}

# Instala um arquivo do stage: cp + chmod 644 + chcon, conferido.
install_one() {
    src="$STAGE/$1"
    dst="$MODS/$1"
    dev "cp $(q "$src") $(q "$dst") && chmod 644 $(q "$dst")" || die "cp/chmod falhou: $dst"
    dev "chcon $CTX $(q "$dst")" || die "chcon falhou em $dst (tipo bepinex_mod_file ausente? módulo instalado?)"
}

dev "rm -rf $(q "$STAGE") && mkdir -p $(q "$STAGE") && chown 2000:2000 $(q "$STAGE") && chmod 775 $(q "$STAGE")" \
    || die "stage $STAGE"
dev "mkdir -p $(q "$MODS") && chmod 755 $(q "$MODS")" || die "mkdir $MODS"
# Pasta criada depois do boot não herda o rótulo do post-fs-data.
dev "chcon $CTX $(q "$MODS")" || die "chcon falhou em $MODS (tipo bepinex_mod_file ausente? módulo instalado?)"
for f in "$@"; do
    adb push "$f" "$STAGE/$(basename "$f")" >/dev/null || die "adb push $f"
done
adb push "$GADGET_BIN" "$STAGE/frida-gadget.bin" >/dev/null || die "adb push $GADGET_BIN"
for f in "$@"; do
    install_one "$(basename "$f")"
done
install_one frida-gadget.bin
# Mesmo JSON que uf_build_config() gera (modo script-directory pra pasta);
# o u_frida recusa qualquer config fora do modo script.
JSON="{\"interaction\":{\"type\":\"script-directory\",\"path\":\"$MODS\",\"on_change\":\"ignore\"}}"
CFG="$MODS/frida-gadget.config"
dev "printf '%s' $(q "$JSON") > $(q "$CFG") && chmod 644 $(q "$CFG")" || die "escrever $CFG"
dev "chcon $CTX $(q "$CFG")" || die "chcon falhou em $CFG"
dev "rm -rf $(q "$STAGE")" || die "limpar $STAGE"
echo "instalado em $MODS (reinicie o jogo)"
