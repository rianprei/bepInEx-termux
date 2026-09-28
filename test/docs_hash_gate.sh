#!/usr/bin/env bash
# test/docs_hash_gate.sh — hash citado como mesclado tem que existir e estar na base.
#
# Cobre exatamente: docs/ROADMAP-UNIVERSAL.md, CHANGELOG.md (raiz) e
# docs/CHANGELOG*.md (se existir algum). Em cada arquivo coberto, extrai
# todo hash de 7 a 40 hexadecimais (com ou sem crase, com limite de palavra)
# das linhas que se declaram mescladas — 'merged', 'mesclado' ou caixa [x]
# (caixa alta também) — e FALHA se o hash não existir (git cat-file) ou não
# for ancestral de HEAD (git merge-base --is-ancestor). Linha que se declara
# mesclada E diz 'fora da base' é CONTRADIÇÃO e falha; linha sem marca de
# merge continua fora do check. Se nenhum arquivo coberto existir, FALHA em
# vez de passar em silêncio.
#
# O QUE "SE DECLARA MESCLADA" SIGNIFICA — e o que NÃO significa. A marca é
# palavra, não substring: 'merged'/'mesclado' com limite de palavra, ou a
# linha começando por '- [x]' em qualquer caixa. NEGAÇÃO conta como o
# contrário: 'unmerged', 'not merged', 'não merged', 'não mesclado' e as
# variantes sem acento declaram que NÃO está mesclado, e a linha é pulada.
# Sem isso, "não merged: <hash de branch>" — que é justamente a linha que
# documenta trabalho pendente — caía no check e falhava, punindo a pessoa
# certa pelo motivo errado.
#
# O LIMITE CONHECIDO, e ele é deliberado: só estas três marcas contam.
# 'integrado', 'done' e 'landed' NÃO são reconhecidos, e uma linha que usa
# só essas palavras fica fora do check. Não é suporte faltando por acidente:
# é o vocabulário que este gate promete conferir. Ampliar a lista é decisão
# de quem mantém o gate, não um efeito colateral de arrumar outra coisa —
# porque o custo de errar aqui é assimétrico. Falso positivo (linha de
# trabalho pendente derrubando o gate) faz o autor desligar a regra; falso
# negativo (linha de merge real escaping) é o que a revisão humana pega. Se
# um dia o vocabulário crescer, que seja escrito aqui e coberto por fixture
# em test/docs_hash_gate_fixture.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

python3 - "$ROOT" <<'PY'
import glob
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
wanted = [root / "docs/ROADMAP-UNIVERSAL.md", root / "CHANGELOG.md"]
wanted += [Path(p) for p in sorted(glob.glob(str(root / "docs/CHANGELOG*.md")))]
covered = [d for d in wanted if d.is_file()]
if not covered:
    print("docs-hash-gate: nenhum documento coberto "
          "(docs/ROADMAP-UNIVERSAL.md, CHANGELOG.md, docs/CHANGELOG*.md)", file=sys.stderr)
    raise SystemExit(1)

ref_re = re.compile(r"\b[0-9a-f]{7,40}\b")

# Marca de merge por PALAVRA, nao por substring: e o que separa "unmerged" e
# "not merged" — que nao estao mergeados — de uma declaracao de merge real.
MERGED_WORD = re.compile(r"\b(?:merged|mesclado)\b", re.IGNORECASE)
# Negacao imediatamente antes da palavra. Cobre 'unmerged' (sem espaco), 'not
# merged', 'nao merged', 'nao mesclado' e as formas com acento.
NEGATED_MERGE = re.compile(
    r"\b(?:un|not|nao|n[ãa]o)\s*[ -]?\s*(?:merged|mesclado)\b", re.IGNORECASE
)


def declares_merged(line: str) -> bool:
    """A linha afirma que algo foi mergeado?

    Verdadeiro se houver 'merged'/'mesclado' como palavra que NAO esteja sob
    uma negacao, ou se a linha for uma caixa '- [x]' em qualquer caixa.
    """
    negations = [m.span() for m in NEGATED_MERGE.finditer(line)]
    for match in MERGED_WORD.finditer(line):
        if any(start <= match.start() and match.end() <= end
               for start, end in negations):
            continue
        return True
    return line.lstrip().lower().startswith("- [x]")


errors = []
checked = 0
for doc in covered:
    try:
        rel = doc.relative_to(root).as_posix()
    except ValueError:
        rel = doc.name
    for line_no, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
        is_merged = declares_merged(line)
        mentions_outside = bool(re.search(r"fora da base", line, re.IGNORECASE))
        if mentions_outside:
            if is_merged:
                errors.append(f"{rel}:{line_no}: contradição: linha se declara "
                              f"mesclada e 'fora da base': {line.strip()[:100]}")
            continue
        if not is_merged:
            continue
        for match in ref_re.finditer(line):
            h = match.group(0)
            checked += 1
            if subprocess.run(["git", "cat-file", "-e", h],
                              cwd=root, capture_output=True).returncode != 0:
                errors.append(f"{rel}:{line_no}: hash inexistente: {h}")
            elif subprocess.run(["git", "merge-base", "--is-ancestor", h, "HEAD"],
                                cwd=root, capture_output=True).returncode != 0:
                errors.append(f"{rel}:{line_no}: fora da base mas marcado como mesclado: {h}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"docs-hash-gate: {checked} hashes conferidos como ancestrais de HEAD "
      f"em {len(covered)} arquivo(s)")
PY
