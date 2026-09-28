#!/usr/bin/env bash
# test/symbols/symbols_read_check.sh — "não consegui ler" e "voltou a stripar"
# são diagnósticos diferentes, e o check que diz um deles tem que provar qual.
#
# O INCIDENTE. Uma execução do verify_all reprovou em "nada entregue leva
# símbolo" com "symbols: libu_noads saiu sem .symtab — o build voltou a
# stripar". O .so tinha .symtab antes, depois e na execução seguinte, e o
# passo passava isolado. Ou seja: o gate  acusou o build de um defeito que ele
# não tinha, com base numa leitura que ninguém podia auditar — porque
# symbols_add fazia `"$readelf" -S "$so" 2>/dev/null` e tratava QUALQUER
# resultado sem `.symtab` como prova de stripping. Falha de ferramenta, E/O,
# arquivo em reescrita e build realmente striparam saíam pela mesma frase, e o
# conserto de quem lesse a mensagem ("olha o jni/repro.mk") apontava para o
# lugar errado.
#
# O QUE ESTE TESTE FAZ. Com um readelf FALSO, ele exercita os três estados que
# a versão anterior confundia em um:
#   1. leitura boa com .symtab   -> guarda, e nada a reclamar;
#   2. leitura boa sem .symtab   -> "voltou a stripar" (o check perde o dente? não);
#   3. leitura impossível        -> "não consegui ler", e NUNCA "voltou a stripar";
#   4. arquivo mudando na leitura-> "não consegui ler" depois de 3 tentativas.
#
# Os casos 3 e 4 são os que o gate errou. Sem eles, a distinção existiria só no
# comentário — que é exatamente onde ela morria antes.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
REAL_READELF="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
MOD="${SYMBOLS_READ_MOD:-u_noads}"
SO="$ROOT/mods/$MOD/libs/arm64-v8a/lib$MOD.so"

die() { echo "symbols_read_check: $*" >&2; exit 1; }
ok() { echo "symbols_read_check: $*"; }

[ -x "$REAL_READELF" ] || die "llvm-readelf do NDK ausente: $REAL_READELF"
if [ ! -f "$SO" ]; then
    [ -x "$NDK/ndk-build" ] || die "sem $SO e sem ndk-build para buildar"
    ( cd "$ROOT/mods/$MOD" && "$NDK/ndk-build" -B -j4 ) >"$WORK/build.log" 2>&1 ||
        { tail -5 "$WORK/build.log" >&2; die "build de mods/$MOD falhou"; }
fi
[ -f "$SO" ] || die "mods/$MOD nao gerou $SO"

# A biblioteca entra com `set -euo pipefail` do lado dela, e sourced ela muda as
# opcoes do shell de quem chama. Aqui isso importa: as chamadas que DEVEM falhar
# (2, 3 e 4) trailed `|| true` de proposito, senao o script morre em silencio
# no primeiro caso negativo — que e o caco de nao se ver aonde morreu.
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"

# readelf falso: `-n` passa (o build-id é real), `-S` é o que a gente mente.
mk_readelf() {
    local mode=$1 path="$WORK/readelf-$1"
    cat > "$path" <<EOS
#!/usr/bin/env bash
mode="$mode"; target="\${@: -1}"
case "\${1:-}" in
    -n) exec "$REAL_READELF" "\$@" ;;
    -S)
        case "\$mode" in
            bom)     exec "$REAL_READELF" "\$@" ;;
            semsym)  printf '  [Nr] Name Type Address Off Size\n'; exit 0 ;;
            falha)   echo "readelf: erro simulado" >&2; exit 1 ;;
            mexe)    printf '  [Nr] Name Type Address Off Size\n'; printf 'x' >> "\$target"; exit 0 ;;
        esac ;;
esac
exit 0
EOS
    chmod +x "$path"
    printf '%s' "$path"
}

# roda symbols_add com um readelf falso e devolve a saida
#   $1 nome do caso (rotulo do diretorio), $2 arquivo de saida, $3 caminho do
#   readelf falso. O readelf entra como parametro de proposito: variavel com o
#   mesmo nome da variavel de fora sombrearia o caminho e o teste passaria
#   usando o readelf de verdade, sem exercitar nada.
run_add() {
    local caso=$1 saida=$2 falso=$3
    SYMBOLS_READELF="$falso" symbols_add "$SO" "$WORK/sym-$caso" "lib$MOD" \
        >"$saida" 2>&1 || true
}

# --- (1) leitura boa com .symtab: guarda e passa ---------------------------
fake="$(mk_readelf bom)"
if ! run_add bom "$WORK/out1" "$fake"; then
    cat "$WORK/out1" >&2
    die "(1) leitura boa de um .so com .symtab foi recusada: $fake"
fi
so_name="lib$MOD.so"
ls "$WORK/sym-bom"/*/"$so_name" >/dev/null 2>&1 ||
    die "(1) o .so nao-stripado nao foi guardado em $WORK/sym-bom"
ok "(1) leitura boa com .symtab: guardado, nada a reclamar"

# --- (2) leitura boa SEM .symtab: e o defeito que o check existe para achar --
fake="$(mk_readelf semsym)"
run_add semsym "$WORK/out2" "$fake"
grep -q 'voltou a stripar' "$WORK/out2" ||
    { cat "$WORK/out2" >&2; die "(2) .so sem .symtab passou: o check perdeu o dente"; }
ok "(2) leitura boa sem .symtab: acusa 'voltou a stripar' (o defeito real)"

# --- (3) leitura IMPOSSIVEL: nao e o build que esta errado ------------------
fake="$(mk_readelf falha)"
run_add falha "$WORK/out3" "$fake"
grep -q 'nao consegui ler as secoes' "$WORK/out3" ||
    { cat "$WORK/out3" >&2; die "(3) readelf quebrado nao virou 'nao consegui ler'"; }
if grep -q 'voltou a stripar' "$WORK/out3"; then
    cat "$WORK/out3" >&2
    die "(3) readelf quebrado virou 'voltou a stripar': e a mentira que o gate  contou"
fi
ok "(3) readelf quebrado: acusa 'nao consegui ler', e nao o build"

# --- (4) o arquivo muda DEPOIS da leitura: 3 tentativas e diagnose certa ---
cp "$SO" "$WORK/instavel.so"
fake="$(mk_readelf mexe)"
out="$(SYMBOLS_READELF="$fake" symbols_add "$WORK/instavel.so" "$WORK/sym-mexe" \
    "lib-instavel" 2>&1 || true)"
printf '%s' "$out" | grep -q 'nao consegui ler as secoes' || {
    printf '%s\n' "$out" >&2
    die "(4) arquivo que muda na leitura nao virou 'nao consegui ler'"
}
printf '%s' "$out" | grep -q 'voltou a stripar' &&
    die "(4) arquivo instavel virou 'voltou a stripar': mesma mentira de (3)"
ok "(4) arquivo reescrito durante a leitura: 'nao consegui ler' apos 3 tentativas"

# --- (5) a costura e' ela mesma coberta: sem SYMBOLS_READELF o NDK volta ----
fake_ndk="$(symbols_readelf)"
[ "$fake_ndk" = "$REAL_READELF" ] ||
    die "(5) sem SYMBOLS_READELF o check tem que usar o readelf do NDK; veio $fake_ndk"
ok "(5) sem a costura de teste, o readelf do NDK e' o escolhido"

echo "symbols_read_check: OK"
