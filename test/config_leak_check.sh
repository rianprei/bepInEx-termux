#!/usr/bin/env bash
# test/config_leak_check.sh — o gate nunca escreve no config do repo real.
#
# Antecedente: user.name=t / user.email=t@t apareceram no config LOCAL do
# ~/repos/bepInEx-termux (autor "t" do merge de simulação). O único código
# que escreve git config é test/docs_hash_gate_fixture.sh, hoje preso ao
# repo temporário via git -C. Este check fotografa o config local antes e
# depois de rodar a fixture e FALHA se qualquer chave mudar/aparecer/sumir.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

snap() { git -C "$ROOT" config --local --list 2>/dev/null || true; }

before=$(snap)
bash "$ROOT/test/docs_hash_gate_fixture.sh" >/dev/null 2>&1
fixture_rc=$?
after=$(snap)

fails=0
if [ $fixture_rc -ne 0 ]; then
    echo "config-leak-check: a fixture falhou (rc=$fixture_rc)" >&2
    fails=1
fi
if [ "$before" != "$after" ]; then
    echo "config-leak-check: o config LOCAL do repo mudou durante a fixture:" >&2
    diff <(printf '%s\n' "$before" | sort) <(printf '%s\n' "$after" | sort) >&2 || true
    fails=1
fi
if [ $fails -ne 0 ]; then
    exit 1
fi
echo "config-leak-check: config local intacto; fixture ok"
