#!/bin/sh
# test/device/mods-reloc-test.sh — a migração da arvore de mods, sem device.
#
# Porque um teste de host e não de device: a garantia que importa aqui é
# "um link simbólico no lugar do diretório NÃO é seguido", e isso é uma
# decisão de shell que roda igual no PC. O device só acrescentaria o domador
# do chmod/chown/chcon.
#
# O que o teste prova, contra uma raiz falsa (nada de /data real é tocado):
#   (a) a migração move o CONTEÚDO de mods/, bc_mods/ e dos .conf;
#   (b) um link simbólico no lugar do diretório antigo NÃO é seguido: o
#       conteúdo apontado não é movido, e o link fica registrado como
#       "não migrado";
#   (c) entrada que não é arquivo regular nem diretório real (fifo) fica
#       onde está e é logada;
#   (d) idempotência: rodar duas vezes não quebra nem duplica;
#   (e) o que a migração NÃO apaga: se sobrou algo no diretório antigo
#       (porque um link recusou), o diretório antigo continua de pé.
# shellcheck disable=SC2015  # as asserções são "A && ok || bad" de propósito:
# ok() e bad() nunca falham, então o A&&B||C é o if-then-else aqui.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MIG="$ROOT/module/migrate-mods-tree.sh"
[ -f "$MIG" ] || { echo "migra-treenao encontrado: $MIG" >&2; exit 1; }

FAILED=0
ok()  { echo "  ok: $1"; }
bad() { echo "  FALHOU: $1"; FAILED=1; }

WORK=$(mktemp -d)
# shellcheck disable=SC2329  # invocada so via "trap cleanup EXIT"
cleanup() { [ -n "${KEEP_REL_ROOT:-}" ] || rm -rf "$WORK"; }
trap cleanup EXIT

PKG=com.fake.game
OLD="$WORK/adb-data-local-tmp"
NEW="$WORK/data/adb/bepinex"
WHY="$WORK/why.log"

# estado inicial: a arvore antiga, com o conteudo de um usuario real
seed() {
    rm -rf "$OLD" "$NEW"
    mkdir -p "$OLD/mods/$PKG" "$OLD/bc_mods"
    printf 'mod do usuario\n' > "$OLD/mods/$PKG/sa2ammo.so"
    printf 'regras do usuario\n' > "$OLD/mods/$PKG/t1.bpatch"
    printf 'lib do bc\n' > "$OLD/bc_mods/libbc.so"
    printf 'appInit=on\n' > "$OLD/bc_mods.conf"
    printf 'com.fake.game\n' > "$OLD/bc_generic_allowlist.conf"
    : > "$WHY"
}

# (a) migra o conteudo
echo "== (a) a migracao move o conteudo =="
seed
sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
[ -f "$NEW/mods/$PKG/sa2ammo.so" ] && ok "sa2ammo.so migrou" || bad "sa2ammo.so nao migrou"
[ -f "$NEW/mods/$PKG/t1.bpatch" ] && ok "t1.bpatch migrou" || bad "t1.bpatch nao migrou"
[ -f "$NEW/bc_mods/libbc.so" ] && ok "bc_mods/libbc.so migrou" || bad "bc_mods/libbc.so nao migrou"
[ -f "$NEW/bc_mods.conf" ] && ok "bc_mods.conf migrou" || bad "bc_mods.conf nao migrou"
[ -f "$NEW/bc_generic_allowlist.conf" ] && ok "allowlist migrou" || bad "allowlist nao migrou"
# e o conteudo migrado e o de verdade, byte a byte
if [ "$(cat "$NEW/mods/$PKG/sa2ammo.so" 2>/dev/null)" = "mod do usuario" ]; then
    ok "o conteudo do arquivo migrado e o original"
else
    bad "o conteudo do arquivo migrado esta errado"
fi
grep -q "migrado" "$WHY" && ok "a migracao logou" || bad "a migracao nao logou"

# (b) link simbolico no lugar do diretório antigo NAO e seguido
echo "== (b) link simbolico no lugar do diretorio antigo NAO e seguido =="
seed
FORA="$WORK/alvo-do-link"
mkdir -p "$FORA/segredo"
printf 'isto nao pode migrar\n' > "$FORA/segredo/plantado.so"
# o atacante (o shell) planta um link no lugar do diretorio de mods
rm -rf "$OLD/mods"
ln -s "$FORA" "$OLD/mods"
sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
# o alvo do link NAO pode ter vazado para a arvore nova
[ ! -e "$NEW/mods/segredo/plantado.so" ] \
    && ok "o conteiro apontado pelo link NAO migrou (o link nao foi seguido)" \
    || bad "SEGUIU O LINK: o conteúdo apontado migrou"
[ ! -e "$NEW/mods/plantado.so" ] \
    && ok "nada do link caiu na arvore nova" \
    || bad "algo do link caiu na arvore nova"
grep -q "link simbolico" "$WHY" \
    && ok "o link foi registrado no log como NAO migrado" \
    || bad "o link nao foi registrado"
# o conteudo do atacante segue intacto onde estava
[ -f "$FORA/segredo/plantado.so" ] \
    && ok "o conteudo apontado pelo link ficou intacto" \
    || bad "o conteudo apontado pelo link sumiu"
# o link em si continua no lugar antigo
[ -L "$OLD/mods" ] && ok "o link continua no diretorio antigo" || bad "o link foi removido"

# (c) fifo (nao e arquivo regular nem diretorio) fica e e logado
echo "== (c) entrada que nao e arquivo regular nem diretorio fica onde esta =="
seed
mkfifo "$OLD/mods/pipe_do_mod" 2>/dev/null || true
if [ -p "$OLD/mods/pipe_do_mod" ]; then
    sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
    [ -p "$OLD/mods/pipe_do_mod" ] \
        && ok "o fifo ficou onde estava" \
        || bad "o fifo foi migrado"
    [ ! -e "$NEW/mods/pipe_do_mod" ] \
        && ok "o fifo nao apareceu na arvore nova" \
        || bad "o fifo apareceu na arvore nova"
    grep -q "fifo" "$WHY" \
        && ok "o fifo foi registrado no log" \
        || bad "o fifo nao foi registrado"
    # e o resto do diretorio migrou mesmo assim
    [ -f "$NEW/mods/$PKG/sa2ammo.so" ] \
        && ok "o resto do diretorio migrou mesmo com o fifo recusado" \
        || bad "o fifo derrubou a migracao inteira"
else
    echo "  (mkfifo indisponivel aqui; caso pulado)"
fi

# (d) idempotencia
echo "== (d) rodar duas vezes nao quebra nem duplica =="
seed
sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
antes=$(find "$NEW/mods/$PKG" -maxdepth 1 -type f 2>/dev/null | wc -l)
sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
depois=$(find "$NEW/mods/$PKG" -maxdepth 1 -type f 2>/dev/null | wc -l)
[ "$antes" = "$depois" ] \
    && ok "a arvore nova nao mudou na segunda passada ($antes entradas)" \
    || bad "a arvore nova mudou na segunda passada ($antes -> $depois)"

# (e) o que nao pode migrar mantem o diretorio antigo de pe
echo "== (e) sobra no antigo mantem o diretorio antigo de pe =="
seed
FORA2="$WORK/alvo-2"
mkdir -p "$FORA2"
printf 'x\n' > "$FORA2/plantado.so"
rm -rf "$OLD/mods"; ln -s "$FORA2" "$OLD/mods"
sh -c ". '$MIG'; bep_migrate_tree '$OLD' '$NEW' '$WHY' mods bc_mods" >/dev/null 2>&1
[ -L "$OLD/mods" ] \
    && ok "o diretorio antigo (com o link) continua de pe para o usuario corrigir" \
    || bad "o diretorio antigo sumiu mesmo com entrada nao migrada"
# e a arvore nova EXISTE e e usavel, mesmo com essa sobra
[ -d "$NEW" ] && ok "a arvore nova existe" || bad "a arvore nova nao existe"

echo
if [ "$FAILED" -ne 0 ]; then
    echo "mods-reloc-test: HOUVE FALHAS" >&2
    exit 1
fi
echo "mods-reloc-test: todos os cenarios passaram"
exit 0
