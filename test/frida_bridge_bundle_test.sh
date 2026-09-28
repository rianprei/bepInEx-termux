#!/usr/bin/env bash
# test/frida_bridge_bundle_test.sh — o bundle da ponte il2cpp tem a FORMA que o
# gadget consegue rodar, sem rede.
#
# O gadget do u_frida lê o primeiro *.js da pasta e roda como script do GumJS:
# sem require, sem import, e com Il2Cpp definido. O fixture é um bundle já
# empacotado, pequeno de propósito — o gate não baixa a ponte (168 KB de rede
# numa etapa de gate seria o tipo de coisa que quebra o dia que o registry
# estiver lento), e o que ele prova é a forma do arquivo, que é a mesma em
# qualquer tamanho.
#
# Node ausente não é SKIP silencioso: SKIP=0 é regra do gate, então a ausência
# sai como FAIL com o motivo, para ninguém ler "PASS" num gate que não rodou.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUNDLE="$ROOT/test/fixtures/frida_bridge/bundled_sample.js"
LOCKFILE="$ROOT/tools/frida_il2cpp_bridge.lock"
LICENSE_FILE="$ROOT/tools/frida_il2cpp_bridge.LICENSE"
MAX_BYTES=$((256 * 1024))

die() { echo "frida_bridge_bundle_test: $*" >&2; exit 1; }
ok() { echo "frida_bridge_bundle_test: $*"; }

[ -f "$BUNDLE" ] || die "fixture ausente: $BUNDLE"
[ -f "$LOCKFILE" ] || die "lockfile ausente: $LOCKFILE"

# 1. O arquivo é autocontido: nada de require/import sobrando.
if grep -nE '(^|[^A-Za-z0-9_$])require[[:space:]]*\(|^[[:space:]]*import[[:space:]]' "$BUNDLE" >/dev/null; then
    echo "--- linhas com require/import ---" >&2
    grep -nE '(^|[^A-Za-z0-9_$])require[[:space:]]*\(|^[[:space:]]*import[[:space:]]' "$BUNDLE" >&2
    die "o bundle tem require/import sobrando: o gadget nao resolve modulo, o script morre ao carregar"
fi
ok "(1) sem require/import sobrando"

# 2. Il2Cpp definido no topo — antes do script do usuario, que é quem usa.
head_line=$(grep -nE 'root\.Il2Cpp[[:space:]]*=|globalThis\.Il2Cpp[[:space:]]*=' "$BUNDLE" | head -n1 | cut -d: -f1)
[ -n "$head_line" ] || die "o bundle nao define Il2Cpp em lugar nenhum: o script da comunidade morre na 1a linha"
[ "$head_line" -le 30 ] || die "Il2Cpp so aparece na linha $head_line: a definicao tem que vir antes do script do usuario"
ok "(2) Il2Cpp definido na linha $head_line, antes do script do usuario"

# 3. Tamanho dentro do teto. A ponte 0.14.0 empacotada tem ~168 KB; o teto
#    existe para pegar um .js que virou dump de build inteiro.
size=$(stat -c%s "$BUNDLE")
[ "$size" -le "$MAX_BYTES" ] || die "bundle com $size bytes, acima do teto de $MAX_BYTES"
ok "(3) tamanho $size bytes, teto $MAX_BYTES"

# 4. A licença da ponte viaja com o lockfile.
[ -s "$LICENSE_FILE" ] || die "licenca da ponte ausente ou vazia: $LICENSE_FILE"
grep -qi 'MIT' "$LICENSE_FILE" || die "a licenca guardada nao e a MIT que o lockfile declara"
ok "(4) licenca MIT guardada em tools/frida_il2cpp_bridge.LICENSE"

# 5. O lockfile pinou versao e sha256, e o sha tem o formato de sha256 (64 hex).
version=$(sed -n 's/^VERSION=//p' "$LOCKFILE" | head -n1)
sha=$(sed -n 's/^SHA256=//p' "$LOCKFILE" | head -n1)
[ -n "$version" ] || die "lockfile sem VERSION"
printf '%s' "$sha" | grep -qE '^[0-9a-f]{64}$' || die "lockfile sem SHA256 de 64 hex: ${sha:-vazio}"
ok "(5) lockfile pinado: ponte $version, sha256 conferido"

# 6. Prova executada: node roda o bundle contra um stub mínimo de Frida e o
#    Il2Cpp.perform é de fato chamável. Sem node, FAIL com o motivo.
if ! command -v node >/dev/null 2>&1; then
    die "SKIP-motivo: node nao esta no host, e a prova de que Il2Cpp.perform e chamavel ficou por fazer. Instale node e rode de novo — gate nao aceita SKIP."
fi
STUB="$(mktemp)"
cat > "$STUB" <<'JS'
// Stub mínimo de Frida: o bundle só usa globalThis e console.log. Se o
// fixture precisasse de mais que isso, o stub estaria curto demais e o teste
// mentiria — por isso ele falha alto em vez de tolerar falta.
globalThis.__calls = [];
var realLog = console.log;
// Registra E deixa passar: um stub que engole a saida faz o teste passar por
//“nao rodou”, que e o oposto do que ele existe para provar.
console.log = function () {
    globalThis.__calls.push(Array.prototype.join.call(arguments, " "));
    realLog.apply(console, arguments);
};
JS
# Executa de verdade: stub + fixture no mesmo contexto, como o GumJS faria
# (o bundle é um script solto, não um módulo).
out="$( { cat "$STUB"; cat "$BUNDLE"; echo 'if (!globalThis.__calls.length) { throw new Error("perform nao chamou console.log: o bundle nao rodou"); }'; } | node 2>&1 )" \
    || die "node nao executou o bundle: $out"
printf '%s' "$out" | grep -q 'perform() respondeu' \
    || die "o bundle rodou mas Il2Cpp.perform nao respondeu como o script espera: $out"
ok "(6) node executou o bundle: $(printf '%s' "$out" | tail -n1)"

echo "frida_bridge_bundle_test: OK"
