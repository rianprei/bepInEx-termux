#!/usr/bin/env bash
# test/docs_hash_gate.sh — hash citado como mesclado tem que existir e estar na base.
#
# docs/ROADMAP-UNIVERSAL.md e docs/CHANGELOG*.md marcam linhas como merged/
# mesclado (ou caixas [x] com hash). Um hash de branch NÃO mesclada escrito
# como 'merged' passa em qualquer gate de conteúdo — a verdade só aparecia
# na revisão humana. Esta etapa extrai todo hash nessas linhas e FALHA se
# ele não existir (git cat-file) ou não for ancestral de HEAD
# (git merge-base --is-ancestor).
#
# Genérico de propósito: release-notes-2 reaproveita sem mudar código, é só
# marcar as linhas do mesmo jeito. O que está fora da base se marca
# "fora da base" e SEM hash de merge — essas linhas são puladas.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

python3 - "$ROOT" <<'PY'
import glob
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
docs = [root / "docs/ROADMAP-UNIVERSAL.md"]
docs += [Path(p) for p in sorted(glob.glob(str(root / "docs/CHANGELOG*.md")))]
docs = [d for d in docs if d.is_file()]

ref_re = re.compile(r"`([0-9a-f]{7,40})`")
errors = []
checked = 0
for doc in docs:
    for line_no, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
        if re.search(r"fora da base", line, re.IGNORECASE):
            continue
        if not re.search(r"merged|mesclado", line, re.IGNORECASE) \
                and not line.lstrip().startswith("- [x]"):
            continue
        for match in ref_re.finditer(line):
            h = match.group(1)
            checked += 1
            if subprocess.run(["git", "cat-file", "-e", h],
                              cwd=root, capture_output=True).returncode != 0:
                errors.append(f"{doc.name}:{line_no}: hash inexistente: {h}")
            elif subprocess.run(["git", "merge-base", "--is-ancestor", h, "HEAD"],
                                cwd=root, capture_output=True).returncode != 0:
                errors.append(f"{doc.name}:{line_no}: fora da base mas marcado como mesclado: {h}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"docs-hash-gate: {checked} hashes conferidos como ancestrais de HEAD")
PY
