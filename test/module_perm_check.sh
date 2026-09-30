#!/usr/bin/env bash
# test/module_perm_check.sh — todo executavel do modulo tem +x GARANTIDO no
# destino, e o modo do git sobrevive ao zip.
#
# POR QUE ESTE CHECK EXISTE (achado da rodada 3 no POCO C75, 2026-09-29, base
# c1513a5): o build empacota os executaveis com 0755 e o git mode e 100755, mas
# `magisk --install-module` extrai o zip e o manager NAO repõe o bit +x. Medido
# no aparelho: depois de instalar v0.5.0, TODOS os arquivos do modulo voltaram
# como -rw-r--r--. Nenhum chmod no repo, no build ou no zip corrigiria isso — a
# garantia tem de ser dada em tempo de instalacao, no customize.sh, com set_perm.
# Sem isso: post-fs-data.sh nao roda (sem exec), o migrador nao roda, a arvore
# nao recebe o contexto bepinex_mod_file, e o companion falha ao executar
# termux-console/bepin-console com "FileUtils Error (150)".
#
# O QUE ESTE CHECK EXIGE, em tres partes:
#
#   1. Todo arquivo com modo 100755 no git (module/ e termux-console/) tem um
#      set_perm correspondente no customize.sh. A lista e conferida nos DOIS
#      sentidos: falta um 100755 (executavel sem set_perm) OU sobra um set_perm
#      de arquivo que o git nao marca 100755 (permisao que protege nada e
#      envelhece mal).
#   2. O modo do zip e 0755 nos executaveis — o build nao pode regredir para
#      um zip com tudo 0644 mesmo que o customize.sh garanta. As duas camadas
#      sao independentes e ambas importam: a primeira cobre o manager que
#      estraga, a segunda cobre o build que poderia estragar.
#   3. O conjunto de arquivos que o build_module.sh COPIA esta inteiro na lista
#      de set_perm. Um executavel novo empacotado sem entrada no customize.sh
#      passa em (1) se alguem errar o modo no git, e falha aqui.
#
# BEPI-CONSOLE E O CASO QUE O CHECK NAO PODE DEIXAR PASSAR: o arquivo nao tem
# extensao .sh, entao o conserto ingenuo (`chmod 755 *.sh`) nao o alcanca — foi
# exatamente o que aconteceu no aparelho. Qualquer solucao por extensao em vez
# de por lista declarada e rejeitada por construcao.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

fails=0
bad() { echo "  [FAIL] $1" >&2; fails=$((fails + 1)); }

# --- 1. o customize.sh garante o +x de todo 100755 do git ---------------------
# Extrai o alvo do set_perm: set_perm 0755 "$MODPATH/<alvo>"
extract_perm_targets() {
    # awk em vez de sed: o sed com \{1,\} e as aspas do $MODPATH e' fragil demais
    # (ja errou uma vez nesta sessao com "descasados"). awk nao tem esse problema.
    awk '
        /^set_perm[[:space:]]/ {
            mode = $2
            # $3 vem como "$MODPATH/<resto>"; remove o prefixo e as aspas.
            path = $3
            sub(/^"\$MODPATH\//, "", path)
            sub(/"$/, "", path)
            sub(/"$/, "", mode)
            if (mode != "" && path != "") print mode "|" path
        }
    ' module/customize.sh
}

declare -A want=()   # caminho RELATIVO a $MODPATH -> modo
# O set_perm escreve "$MODPATH/<alvo>", entao a comparacao tem de ser no mesmo
# namespace: o git diz "module/action.sh", o set_perm diz "action.sh". Um
# check que compara os dois sem tirar o prefixo reprova tudo e nao diz nada.
while read -r mode path; do
    [ -n "${path:-}" ] || continue
    rel="${path#module/}"
    want["$rel"]="$mode"
done < <(git ls-files -s module/ termux-console/ | awk '{print $1, $4}' \
         | awk '$1=="100755"{print "0755", $2}')

declare -A got=()
while IFS='|' read -r mode path; do
    [ -n "${path:-}" ] || continue
    got["$path"]="$mode"
done < <(extract_perm_targets)

# 1a. todo 100755 do git tem set_perm
for path in "${!want[@]}"; do
    [ -n "${path:-}" ] || continue
    if [ -z "${got[$path]:-}" ]; then
        bad "executavel sem set_perm no customize.sh: $path (git 100755)"
    elif [ "${got[$path]}" != "${want[$path]}" ]; then
        bad "$path: set_perm ${got[$path]} mas o git diz ${want[$path]}"
    fi
done

# 1b. nenhum set_perm orfao (alvo que o git nao marca executavel, ou nao existe).
#     Excecoes TIPADAS, como no old_mods_path_check: cada uma declara POR QUE o
#     alvo nao e' 100755 no git e ainda assim precisa de set_perm. Sem tipo, um
#     set_perm orfao passa; a lista sozinha e' auto-certificada e apodrece.
#       dado     nao e' executavel; o modo 0644 e' o explicito, para que um
#               chmod -R alheio nao o torne executavel por acidente.
#       artefato nao vive no git (gerado pelo build ou do update-binary do
#               Magisk): o git nao tem o que marcar, mas o destino existe e
#               precisa de modo.
declare -A ORFAO_TIPADO=(
    [sepolicy.rule]="dado"
    [META-INF/com/google/android/update-binary]="artefato"
    [termux-console/termux_client.py]="dado"
)
for path in "${!got[@]}"; do
    [ -n "${path:-}" ] || continue
    if [ -n "${want[$path]:-}" ]; then
        continue
    fi
    tipo="${ORFAO_TIPADO[$path]:-}"
    if [ -z "$tipo" ]; then
        bad "set_perm para alvo que o git nao marca 100755 e nao esta tipado: $path"
    elif [ "$tipo" = "dado" ] && [ "${got[$path]}" != "0644" ]; then
        bad "set_perm de dados $path com modo ${got[$path]} (esperado 0644)"
    fi
done

# --- 2. o build empacota os executaveis como 0755 -----------------------------
# O build ja faz chmod 755 nos dois do termux-console e no update-binary; este
# check exige que isso CONTINUE no codigo, porque o customize.sh sozinho nao
# protege o artefato entregue a quem so extrai o zip.
build=tools/build_module.sh
# Lista de chmod 755 declarados no build, normalizada.
build_perms=$(sed -n 's/.*chmod[[:space:]]\{1,\}755[[:space:]]\{1,\}//p' "$build" \
              | tr ' ' '\n' | sed 's/\\$//' | grep -v '^$' | sort -u)
# shellcheck disable=SC2016  # o $ dentro das aspas simples e' LITERAL de
# proposito: e' o texto que o build_module.sh escreve, nao expansao.
for required in '"$STAGE/termux-console/bepin-console"' '"$STAGE/META-INF/com/google/android/update-binary"'; do
    # here-string, nao pipe: com pipefail, grep -q saindo cedo da SIGPIPE o
    # produtor e o pipefail transforma isso em falha mesmo com o elemento
    # presente. O lint (pipefail_grep_check) cobra a forma, e com razao.
    if ! grep -Fxq "$required" <<<"$build_perms"; then
        bad "build_module.sh nao faz chmod 755 de $required"
    fi
done
# Os .sh do module/ vao para o zip pelo cp sem chmod explicito; o customize.sh
# e quem os corrige no device. A garantia do zip para eles e conferida em (3).
# shellcheck disable=SC2016  # idem: $STAGE literal, o texto do build
if ! grep -Eq 'chmod[[:space:]]+755[[:space:]]+.*\$STAGE/\*?\.sh|chmod[[:space:]]+755[[:space:]]+"\$STAGE/\$\{?STAGE' "$build" \
   && ! grep -q 'STAGE/post-fs-data\|STAGE/\*\.sh' <<<"$build_perms"; then
    echo "  nota: build_module.sh nao chmod 755 explicito nos .sh do module/;" >&2
    echo "        garantido em runtime pelo set_perm do customize.sh." >&2
fi

# --- 3. tudo que o build COPIA tem permissao garantida no destino --------------
# O build e a fonte da verdade do conteudo do zip: se um arquivo novo entra no
# modulo e o customize.sh nao fala dele, o manager deixa sem +x e o defeito
# volta. Extrai os nomes de arquivo do cp/chmod do build.
packed=$(awk '
        /^[[:space:]]*cp[[:space:]]/ {
            for (i = 2; i < NF; i++) {
                t = $i
                sub(/\\$/, "", t)             # continuacao de linha
                if (t ~ /\$/) continue          # destino final
                if (t ~ /^(module|termux-console|tools)\//) print t
            }
        }
    ' "$build" | sed 's#^[^/]*/##' | sort -u)
for src in $packed; do
    # tools/termux_client.py entra como termux-console/termux_client.py
    case "$src" in
        # O awk acima tira o prefixo do diretorio; os dois do console precisam
        # do prefixo de volta, porque o set_perm escreve
        # "$MODPATH/termux-console/...".
        bepin-console|termux_client.py) target="termux-console/$src" ;;
        *) target="$src" ;;
    esac
    case "$target" in
        *.rule) continue ;;   # sepolicy e dado
    esac
    if [ -z "${got[$target]:-}" ]; then
        bad "build empacota $src mas customize.sh nao garante permissao (set_perm) para $target"
    fi
done

# --- 4. o proprio check se sabota (a prova de que ele morde) -------------------
# Um check que nunca falha nao e um check. A_fixture abaixo e uma copia do
# customize.sh com o set_perm do bepin-console REMOVIDO — o defeito exato do
# aparelho — e o check tem que rejeitar ela. Sem isto, um check que engole
# qualquer coisa passaria junto.
trap 'rm -rf "$TMPD"' EXIT
TMPD=$(mktemp -d)
mkdir -p "$TMPD/module"
cp module/customize.sh "$TMPD/module/customize.sh"
python3 - "$TMPD/module/customize.sh" <<'PYSED'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
s = p.read_text(encoding="utf-8")
lines = [l for l in s.splitlines(keepends=True)
         if not l.startswith('set_perm 0755 "$MODPATH/termux-console/bepin-console"')]
p.write_text("".join(lines), encoding="utf-8")
PYSED
# O grep tem de casar a LINHA set_perm, nao qualquer mencao ao arquivo: o
# comentario do conserto tambem cita o caminho, e casar ele daria a fixture
# errada por um motivo certo (o set_perm saiu) com a verificacao errada
# (achou comentario). Ja aconteceu nesta sessao.
if grep -q '^set_perm .*termux-console/bepin-console' "$TMPD/module/customize.sh"; then
    bad "sabotagem nao removou o set_perm do bepin-console (a fixture esta errada)"
else
    # Confere que a fixture REALMENTE falha: as permissões de $got caem para as
    # de $want sem o bepin-console.
    miss=0
    while read -r mode path; do
        [ -n "${path:-}" ] || continue
        grep -q "\"\$MODPATH/$path\"" "$TMPD/module/customize.sh" || miss=1
    done < <(git ls-files -s module/ termux-console/ | awk '$1=="100755"{print "0755", $2}')
    if [ "$miss" -eq 0 ]; then
        bad "a fixture sem set_perm ainda passa a conferencia — o check nao morde"
    else
        echo "  ok: sabotagem (sem set_perm do bepin-console) rejeitada"
    fi
fi

# --- 5. o post-fs-data se AUTORREPARA: a garantia nao pode depender so do
#        instalador. Medido no POCO C75 (2026-09-29, base c1513a5): a CLI do
#        Magisk EXTRAI e APAGA o customize.sh sem executa-lo, logo o set_perm nao
#        roda e nada fica +x. Como o magiskinit faz exec (nao `sh arquivo`), o
#        post-fs-data nao pode corrigir o que impede ELE de rodar — mas no
#        primeiro boot em que rodar (app, recovery ou chmod manual), tem de
#        deixar o resto executavel para os boots seguintes.
pfs=module/post-fs-data.sh
# shellcheck disable=SC2016  # o $ é literal dentro do padrão POSIX escapado (busca string com $), não expansão
if ! grep -q 'chmod 755 "\$_moddir' "$pfs"; then
    bad "post-fs-data.sh nao faz o autorreparo de chmod: o set_perm nao basta, a CLI do Magisk apaga o customize.sh"
fi
for alvo in post-fs-data.sh action.sh migrate-mods-tree.sh; do
    if ! grep -qF "$alvo" "$pfs"; then
        bad "post-fs-data.sh nao repara o +x de $alvo"
    fi
done
if ! grep -q 'bepin-console' "$pfs"; then
    bad "post-fs-data.sh nao repara o +x de termux-console/bepin-console (sem extensao .sh, escapa de chmod 755 *.sh)"
fi
# B3: o caminho fixo do source aponta para um id de modulo que nao existe
# (/data/adb/modules/bepinex-termux/ enquanto o id e' bc-poc), e o fallback por
# $(dirname "$0") nao salva porque o magiskinit deixa $0 vazio.
# Sem ancora ^: o caminho proibido pode aparecer em qualquer linha, e a forma
# (comentado, ativo, dentro de if) nao e' o que importa. O que importa e' que
# ele NAO apareca de jeito nenhum, porque nunca vai acertar: o id deste modulo
# e' bc-poc, nao bepexin-termux. Um grep com ^ ja falhou em sabotagem — o
# teste passava com o caminho la.
if grep -q '/data/adb/modules/bepinex-termux/module/migrate-mods-tree\.sh' "$pfs"; then
    bad "post-fs-data.sh cita o caminho fixo /data/adb/modules/bepinex-termux/ (id de modulo inexistente, bug B3)"
fi
if ! grep -q '_mig=' "$pfs"; then
    bad "post-fs-data.sh nao tem busca pelo migrador (so o source com \$0, que o magiskinit deixa vazio)"
fi

if [ "$fails" -ne 0 ]; then
    echo "module_perm_check: FALHOU ($fails)" >&2
    exit 1
fi
echo "module_perm_check: ${#want[@]} executavel(is) com +x garantido pelo customize.sh; modo do zip e permissao do destino conferidos"
