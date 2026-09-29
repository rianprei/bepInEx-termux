#!/usr/bin/env bash
# test/frida_bundle_extract_test.sh — o empacotador recusa tarball com entrada
# que não é arquivo regular nem diretório, OFFLINE.
#
# O furo que este teste cobre: um link simbólico dentro do pacote faz o `cat` da
# entrada embutir no .js que o jogo carrega um arquivo do DISCO de quem roda a
# ferramenta. O sha256 pinado garante que o pacote é o publicado; não garante
# que ele não traga um link apontando para fora. Por isso a checagem é de
# TIPO, e acontece antes da extração.
#
# Os tarballs são gerados aqui mesmo, com o mesmo lockfile e o mesmo sha256 que
# a ferramenta usa. Sem rede, sem o pacote de verdade: o que se prova é a
# recusa, e ela não depende do conteúdo do pacote.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOOL="$ROOT/tools/bundle_frida_script.sh"
LOCKFILE="$ROOT/tools/frida_il2cpp_bridge.lock"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
die() { echo "frida_bundle_extract_test: $*" >&2; exit 1; }
ok() { echo "frida_bundle_extract_test: $*"; }

[ -x "$TOOL" ] || die "ferramenta ausente: $TOOL"

# Congela o download: a ferramenta baixa da URL do lockfile, e aqui ela aponta
# para um tarball local com o MESMO sha256 que o lockfile exige.
cp "$LOCKFILE" "$WORK/orig.lock"
# A ferramenta tambem escreve a licenca do pacote ao lado do lockfile, e com um
# tarball falso isso sobrescreveria a licenca REAL de um arquivo versionado. O
# teste restaura os dois, ou ele mesmo vira o que o caso (1) proibe.
cp "$ROOT/tools/frida_il2cpp_bridge.LICENSE" "$WORK/orig.LICENSE"
# A URL do lockfile passa a apontar para o tarball local por file://, que o
# curl fala sem rede, e o SHA256 passa a ser o do tarball local. Os dois juntos
# sao o que a ferramenta confere, entao o caminho exercitado e o de verdade.
mk() { # mk <arquivo.tgz>
    got="$(sha256sum "$1" | cut -d' ' -f1)"
    sed -e "s#^URL=.*#URL=file://$1#" -e "s#^SHA256=.*#SHA256=$got#" \
        "$WORK/orig.lock" > "$LOCKFILE"
}
restore() {
    cp "$WORK/orig.lock" "$LOCKFILE"
    cp "$WORK/orig.LICENSE" "$ROOT/tools/frida_il2cpp_bridge.LICENSE"
}
trap 'restore; rm -rf "$WORK"' EXIT

# O que o script do usuario usa, minimo.
printf 'var x = 1;\n' > "$WORK/user.js"
SECRETO="$WORK/segredo-do-host"
printf 'SENHA-DO-HOST-NAO-VAI-PARA-O-JOGO\n' > "$SECRETO"

# tarball legitimo: package/dist/index.js + package/LICENSE, arquivos comuns
LEGIT_DIR="$WORK/legit"; mkdir -p "$LEGIT_DIR/package/dist"
printf '(function(){ globalThis.Il2Cpp = { v: "fake" }; })();\n' > "$LEGIT_DIR/package/dist/index.js"
printf 'MIT fake\n' > "$LEGIT_DIR/package/LICENSE"
tar czf "$WORK/legit.tgz" -C "$LEGIT_DIR" package

# tarball com link simbolico: a entrada aponta para o segredo do host
SYM_DIR="$WORK/sym"; mkdir -p "$SYM_DIR/package/dist"
ln -sf "$SECRETO" "$SYM_DIR/package/dist/index.js"
printf 'MIT fake\n' > "$SYM_DIR/package/LICENSE"
tar czf "$WORK/sym.tgz" -C "$SYM_DIR" package

# tarball com HARDLINK de verdade: o link tem que apontar para outro arquivo
# DENTRO do pacote. Apontando para fora, o tar copia o conteudo e grava tipo
# regular -- o fixture viraria um arquivo comum e nao provaria nada.
HL_DIR="$WORK/hard"; mkdir -p "$HL_DIR/package/dist"
printf 'alvo, dentro do pacote\n' > "$HL_DIR/package/LICENSE_TARGET"
ln "$HL_DIR/package/LICENSE_TARGET" "$HL_DIR/package/dist/index.js"
tar czf "$WORK/hard.tgz" -C "$HL_DIR" package
tar tvzf "$WORK/hard.tgz" | cut -c1 | tr '\n' ' ' | sed 's/^/  tipos no fixture hardlink: /'; echo

run_tool() { # run_tool <tgz> <saida>
    mk "$1"
    rm -f "$2"
    bash "$TOOL" "$WORK/user.js" "$2" 2>&1
}

# (1) symlink: recusa, e o segredo do host nao aparece em lugar nenhum
out="$(run_tool "$WORK/sym.tgz" "$WORK/out-sym.js")" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "aceitou tarball com link simbolico (exit 0)"
[ ! -e "$WORK/out-sym.js" ] || die "gerou saida com o tarball de link simbolico"
grep -qiE 'link|simbolic|regular' <<<"$out" ||
    die "a recusa nao diz que o problema e o tipo da entrada: $out"
if grep -q 'SENHA' <<<"$out"; then die "o segredo do host apareceu na saida"; fi
ok "(1) link simbolico recusado, sem saida e sem vazar o arquivo do host"

# (2) hardlink para fora: mesma recusa
out="$(run_tool "$WORK/hard.tgz" "$WORK/out-hard.js")" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "aceitou tarball com hardlink (exit 0)"
[ ! -e "$WORK/out-hard.js" ] || die "gerou saida com o tarball de hardlink"
ok "(2) hardlink para fora recusado"

# (3) tarball legitimo: funciona, e a extracao nao assume owner/modo do pacote
out="$(run_tool "$WORK/legit.tgz" "$WORK/out-ok.js")" && rc=0 || rc=$?
[ "$rc" -eq 0 ] || die "recusou tarball legitimo: $out"
[ -f "$WORK/out-ok.js" ] || die "nao gerou saida no tarball legitimo"
grep -q 'Il2Cpp' "$WORK/out-ok.js" || die "a saida nao tem a ponte"
ok "(3) tarball legitimo gera bundle"

restore
echo "frida_bundle_extract_test: OK"
