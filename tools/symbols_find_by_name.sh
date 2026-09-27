#!/usr/bin/env bash
# Procura, em $SYMDIR, os build-ids de um .so pelo NOME do arquivo.
#
# POR QUE ISTO EXISTE: tombstone de Android 8- e anteriores não escreve BuildId
# no backtrace, e sem esta busca o crash de um aparelho antigo não cruza com os
# símbolos guardados.
#
# POR QUE ELE DEVIA DEVOLVER TODOS E NÃO SÓ O PRIMEIRO (achado da revisão de
# f59e9ff): com duas builds do mesmo nome guardadas, a versão anterior devolvia
# o PRIMEIRO build-id do INDEX e o symbolize.sh resolvia o offset contra ele sem
# aviso nenhum. No crash real do SA2 isso imprimiu, com toda a confiança de um
# resultado verificado, a função de um build diferente da que o aparelho rodou.
# O dev perde horas na função errada.
#
# A versão anterior ainda carregava um comentário dizendo "o symbolize.sh avisa
# quando há ambiguidade" — e não existe aviso nenhum. Este arquivo não promete o
# que o chamador não faz: devolve a LISTA e deixa a decisão com o chamador.
#
# Saída: um build-id por linha, na ordem do INDEX. Vazio = nenhum candidato.
#　　　  (2+ linhas = ambíguo; quem chama tem de recusar e listar.)
set -euo pipefail
name="$(basename "${1:-}" .so)"
[ -n "$name" ] || exit 0
[ -f "${SYMDIR:?}/INDEX" ] || exit 0
while IFS=$'\t' read -r bid nm _; do
    [ "$nm" = "$name" ] || continue
    printf '%s\n' "$bid"
done < <(sort -u "${SYMDIR:?}/INDEX" 2>/dev/null || true)
exit 0
