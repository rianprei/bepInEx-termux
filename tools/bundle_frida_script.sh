#!/usr/bin/env bash
# tools/bundle_frida_script.sh <script.js|script.ts> <saida.js>
#
# Gera UM .js autocontido: a ponte frida-il2cpp-bridge embutida + o script do
# usuário, na ordem, para o gadget do u_frida rodar como qualquer outro .js da
# pasta. Sem isto o script da comunidade que usa `Il2Cpp.perform` não roda: o
# gadget não tem require e a bridge não vem mais no runtime do Frida 17
# (https://frida.re/docs/bridges/).
#
# REDE SÓ AQUI. Esta ferramenta baixa o tarball pinado; o gate nunca baixa nada
# e confere a estrutura de um fixture versionado (test/frida_bridge_bundle_test.sh).
#
# O SHA-256 vem do lockfile e é conferido ANTES de extrair. Sha errado é
# recusa: é o que impede um tarball trocado de entrar no bundle.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCKFILE="$ROOT/tools/frida_il2cpp_bridge.lock"

die() { echo "bundle_frida_script: $*" >&2; exit 1; }

[ "$#" -eq 2 ] || die "uso: $0 <script.js|script.ts> <saida.js>"
SCRIPT="$1"; OUT="$2"
[ -f "$SCRIPT" ] || die "script ausente: $SCRIPT"
[ -f "$LOCKFILE" ] || die "lockfile ausente: $LOCKFILE"

lock() { sed -n "s/^$1=//p" "$LOCKFILE" | head -n1; }
VERSION="$(lock VERSION)"; URL="$(lock URL)"; SHA="$(lock SHA256)"
ENTRY="$(lock ENTRY)"; LIC="$(lock LICENSE_ENTRY)"; PKG_LIC="$(lock LICENSE)"
for pair in "VERSION:$VERSION" "URL:$URL" "SHA256:$SHA" "ENTRY:$ENTRY" "LICENSE_ENTRY:$LIC"; do
    [ -n "${pair#*:}" ] || die "lockfile sem ${pair%%:*}"
done
case "$SCRIPT" in
    *.js|*.ts) ;;
    *) die "o script tem que ser .js ou .ts: $SCRIPT" ;;
esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

command -v curl >/dev/null 2>&1 || die "curl ausente: baixe o tarball em $URL e ponha em $WORK/f.tgz"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum ausente: sem ele a conferencia do pinado nao existe"
echo "bundle_frida_script: baixando frida-il2cpp-bridge $VERSION" >&2
curl -sSL --fail --max-time 180 -o "$WORK/pkg.tgz" "$URL" ||
    die "falha no download de $URL"

got="$(sha256sum "$WORK/pkg.tgz" | cut -d' ' -f1)"
if [ "$got" != "$SHA" ]; then
    die "sha256 do tarball divergiu do pinado.
     esperado (lockfile): $SHA
     obtido (download):   $got
     Recusa-se extrair: um tarball trocado viraria bridge dentro do .js que o
     game carrega. Se a versao do lockfile foi atualizada de proposito, o sha
     novo vem de recalcular sobre o download conferido no registry."
fi

# O QUE ENTRA NO TARBALL, ANTES DE EXTRAIR. Um link simbolico no pacote faz
# o `cat` do passo seguinte embutir no .js um arquivo do DISCO de quem roda a
# ferramenta — o sha256 pinado garante que o pacote e o que foi publicado, mas
# nao que ele nao traga um link apontando para fora. O type field do `tar tv`
# diz o que cada entrada e, e a extracao so aceita o que um pacote deve ter.
listing="$(tar tvzf "$WORK/pkg.tgz")" || die "nao consegui listar o tarball: $WORK/pkg.tgz"
bad_entries="$(printf '%s\n' "$listing" | awk '
    substr($1,1,1) == "-" || substr($1,1,1) == "d" { next }
    { print }
')"
if [ -n "$bad_entries" ]; then
    echo "bundle_frida_script: o tarball tem entrada que nao e arquivo regular nem diretorio," >&2
    echo "  e um link ou dispositivo no pacote vaza o disco de quem roda a ferramenta" >&2
    echo "  para dentro do .js que o jogo carrega. Entradas recusadas:" >&2
    printf '  %s\n' "$bad_entries" | head -n 5 >&2
    printf '  (total: %s)\n' "$(printf '%s\n' "$bad_entries" | wc -l | tr -d ' ')" >&2
    exit 1
fi

# --no-same-owner e --no-same-permissions: o pacote nao decide uid nem modo do
# que ele escreve no nosso disco, e um link dentro dele ja foi recusado acima.
tar xzf "$WORK/pkg.tgz" -C "$WORK" --no-same-owner --no-same-permissions ||
    die "tarball invalido"

# Defense in depth na entrada: -L antes de -f, porque -f segue link.
[ -L "$WORK/$ENTRY" ] && die "a entrada do pacote e um link simbolico: $ENTRY"
[ -f "$WORK/$ENTRY" ] || die "entrada ausente ou nao regular no tarball: $ENTRY (pacote empacotado diferente do pinado?)"

mkdir -p "$(dirname "$OUT")"
{
    printf '// Gerado por tools/bundle_frida_script.sh — nao editar a mao.\n'
    printf '// Ponte: frida-il2cpp-bridge %s (licenca %s, vfsfitvnm)\n' "$VERSION" "$PKG_LIC"
    printf '// Fonte: %s\n' "$URL"
    printf '// sha256 do pacote: %s\n' "$SHA"
    printf '// Licenca completa da ponte: tools/frida_il2cpp_bridge.LICENSE\n'
    printf '// A ponte define globalThis.Il2Cpp; o script do usuario vem depois.\n\n'
    cat "$WORK/$ENTRY"
    printf '\n\n// ---- script do usuario: %s ----\n' "$(basename "$SCRIPT")"
    cat "$SCRIPT"
} >"$OUT"

# A licenca viaja com o lockfile, nao dentro do .js: o arquivo que o game le
# fica menor e a licerca fica onde se consulta.
cp "$WORK/$LIC" "$ROOT/tools/frida_il2cpp_bridge.LICENSE"

echo "bundle_frida_script: wrote $OUT ($(stat -c%s "$OUT") bytes, ponte $VERSION + $(basename "$SCRIPT"))" >&2
