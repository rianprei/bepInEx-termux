#!/usr/bin/env bash
# test/config_leak_check.sh — o gate nunca escreve no repo de verdade.
#
# ANTECEDENTE, em duas etapas, e a segunda é a que este check existia para
# fechar e não fechava.
#
# 1) user.name=f / user.email=f@t apareceram no config LOCAL do
#    ~/repos/bepInEx-termux, e o merge 791966d saiu com autor "f <f@t>". A
#    origem foi a fixture: test/docs_hash_gate_fixture.sh roda `git config`
#    dentro de um repo temporário, e o caminho depende de um `cd "$TREE"` que
#    fica duas linhas acima. Qualquer refactor que tire esse cd — ou o
#    `git -C "$TREE"` que o prende ao repo certo — escreve no config
#    COMPARTILHADO: worktrees dividem .git/config, então o estrago aparece no
#    repo principal mesmo com a fixture rodando num worktree.
#
# 2) O SINTOMA MAIS GRAVE, que este check agora cobre: no mesmo caminho a
#    fixture não só escreve config, ela COMPRA. Sem o cd, o `git init` e os
#    dois `git commit` da fixture rodam no repo de verdade, e o HEAD dele
#    passa a apontar para um commit chamado "lado" que ninguém escreveu.
#    Medido: HEAD 3124554 antes, HEAD 562534e "lado" depois.
#    Um check que olha SÓ o config passa por cima disso, porque o config é a
#    menor das consequências — e foi exatamente o que aconteceu.
#
# Por isso este check fotografa QUATRO coisas e compara as quatro: o config
# local, o HEAD, o conjunto de refs e a árvore de trabalho. Qualquer uma que
# mude é FAIL, e o diff das refs sai impresso para o maintainer ver QUAL branch
# apareceu.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# O config que importa é o COMPARTILHADO. Num worktree, `git config --local`
# lê $GIT_COMMON_DIR/config — que é o do repo principal. Por isso o check
# funciona igual rodando no repo principal ou num worktree dele.
snap_config() { git -C "$ROOT" config --local --list 2>/dev/null | sort || true; }
snap_head()   { git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo ausente; }
snap_refs()   { git -C "$ROOT" for-each-ref --format='%(refname) %(objectname)' 2>/dev/null | sort || true; }
snap_status() { git -C "$ROOT" status --porcelain 2>/dev/null | sort || true; }

before_config=$(snap_config)
before_head=$(snap_head)
before_refs=$(snap_refs)
before_status=$(snap_status)

bash "$ROOT/test/docs_hash_gate_fixture.sh" >/dev/null 2>&1
fixture_rc=$?

after_config=$(snap_config)
after_head=$(snap_head)
after_refs=$(snap_refs)
after_status=$(snap_status)

fails=0
if [ "$fixture_rc" -ne 0 ]; then
    echo "config-leak-check: a fixture falhou (rc=$fixture_rc)" >&2
    fails=1
fi
if [ "$before_config" != "$after_config" ]; then
    echo "config-leak-check: o config LOCAL do repo mudou durante a fixture:" >&2
    diff <(printf '%s\n' "$before_config") <(printf '%s\n' "$after_config") >&2 || true
    fails=1
fi
# --- o que faltava: HEAD, refs e arvore -------------------------------------
if [ "$before_head" != "$after_head" ]; then
    echo "config-leak-check: o HEAD do repo de verdade ANDOU durante a fixture." >&2
    echo "    antes:  $before_head" >&2
    echo "    depois: $after_head ($(git -C "$ROOT" log -1 --format=%s "$after_head" 2>/dev/null))" >&2
    echo "    isto e a fixture COMMITANDO no repo de verdade: o cd [TREE] sumiu" >&2
    echo "    e o git init/git commit cairam no repo em vez do temporario." >&2
    fails=1
fi
if [ "$before_refs" != "$after_refs" ]; then
    echo "config-leak-check: refs do repo de verdade mudaram durante a fixture:" >&2
    diff <(printf '%s\n' "$before_refs") <(printf '%s\n' "$after_refs") >&2 || true
    fails=1
fi
if [ "$before_status" != "$after_status" ]; then
    echo "config-leak-check: a arvore de trabalho do repo de verdade mudou durante a fixture:" >&2
    diff <(printf '%s\n' "$before_status") <(printf '%s\n' "$after_status") >&2 || true
    fails=1
fi

if [ "$fails" -ne 0 ]; then
    exit 1
fi
echo "config-leak-check: config local, HEAD, refs e arvore intactos; fixture ok"
