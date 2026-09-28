#!/usr/bin/env bash
# test/old_mods_path_check.sh — nenhum caminho velho de árvore de mods
# (/data/local/tmp/mods) fora da lista EXPLÍCITA de exceções abaixo.
#
# POR QUE ESTE CHECK EXISTE (P2 da pré-revisão): a árvore de mods mudou para
# /data/adb/bepinex/mods (root-only). Cada menção perdida ao caminho velho é
# ou uma instrução que não funciona mais, ou um texto que mente sobre onde o
# mod mora. O grep cru achava uma por commit de revisão — agora é o gate que
# acha, com as exceções JUSTIFICADAS uma a uma.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

# EXCEÇÕES (arquivo:conteúdo — cada uma com o motivo). Nada entra aqui sem
# justificativa escrita:
#   module/uninstall.sh            — limpa a árvore VELHA na desinstalação:
#                                    upgrade de quem ainda tem mods no lugar
#                                    antigo precisa do limpador (intencional).
#   module/migrate-mods-tree.sh    — é o próprio migrador: nasceu citando o
#                                    lugar de onde sai a árvore.
#   docs/DEVICE-ROUND.md          — registro HISTÓRICO da rodada 1, executada
#                                    quando a árvore ainda era a antiga.
#   docs/DEVICE-ROUND-2.md        — idem rodada 2; o passo 7 (roteiro FUTURO
#                                    da árvore nova) cita o caminho velho só
#                                    para dizer o que o device_test.sh antigo
#                                    não prova mais.
#   CHANGELOG.md                  — registro histórico de releases: diz o que
#                                    ERA em cada versão; reescrever mudaria a
#                                    história do que foi lançado.
#   RootInjectionTableTest.java   — o caminho é DADO de ataque (fixture de
#                                    injeção no su), não destino de instalação.
#   test/device/restore-sim.sh    — simula o device_test.sh LEGADO (o alvo do
#                                    sim é o script que ainda tira snapshot
#                                    da árvore antiga; o roteiro novo é o
#                                    passo 7 do DEVICE-ROUND-2).
#   test/fuzz/* + seed_corpus.py  — corpus de fuzz: strings de JSON são
#                                    ENTRADA aleatória do validador, não
#                                    instrução; qualquer path serve.
#   symbolize_*_test.sh           — fixture de TOMBSTONE: o path do .so num
#                                    crash report é dado de OUTRA pessoa,
#                                    exatamente o que o teste provoca.
#   ndk_path_test.sh              — comentário que explica por que o TESTE
#                                    cita caminho de aparelho (dado).
#   tools/device_round2_soak.sh    — monitor que casa os DOIS caminhos (o
#                                    grep precisa achar o velho em log
#                                    histórico).
EXCEPTIONS=(
    "module/uninstall.sh"
    "module/migrate-mods-tree.sh"
    "docs/DEVICE-ROUND.md"
    "docs/DEVICE-ROUND-2.md"
    "CHANGELOG.md"
    "manager/test/io/github/rianprei/bepinex/manager/test/RootInjectionTableTest.java"
    "test/device/restore-sim.sh"
    "test/fuzz/corpus/frida_config/cfg_00"
    "test/fuzz/corpus/frida_config/cfg_01"
    "test/fuzz/seed_corpus.py"
    "test/symbols/ndk_path_test.sh"
    "test/symbols/symbolize_injection_test.sh"
    "test/symbols/symbolize_test.sh"
    "tools/device_round2_soak.sh"
)

fails=0
while IFS= read -r hit; do
    file=${hit%%:*}
    ok=0
    for ex in "${EXCEPTIONS[@]}"; do
        [ "$file" = "$ex" ] && { ok=1; break; }
    done
    if [ "$ok" = 0 ]; then
        echo "  [FAIL] caminho velho fora das exceções: $hit" >&2
        fails=$((fails + 1))
    fi
done < <(git grep -n "data/local/tmp/mods" -- . ':!test/old_mods_path_check.sh' || true)

if [ "$fails" -ne 0 ]; then
    echo "old_mods_path: $fails menção(ões) fora das exceções — migre o texto para /data/adb/bepinex/mods ou documente a exceção AQUI com justificativa" >&2
    exit 1
fi
echo "old_mods_path: nenhuma menção a /data/local/tmp/mods fora das ${#EXCEPTIONS[@]} exceções justificadas"
