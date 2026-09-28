#!/usr/bin/env bash
# test/docs_hash_gate.sh — hash citado como mesclado tem que existir e estar na base.
#
# Cobre exatamente: docs/ROADMAP-UNIVERSAL.md, CHANGELOG.md (raiz) e
# docs/CHANGELOG*.md (se existir algum). Em cada arquivo coberto, extrai
# todo hash de 7 a 40 hexadecimais (com ou sem crase, com limite de palavra)
# das linhas que se declaram mescladas — 'merged', 'mesclado' ou caixa [x]
# — e FALHA se o hash não existir (git cat-file) ou não for ancestral de
# HEAD (git merge-base --is-ancestor). Linha que se declara mesclada E diz
# 'fora da base' é CONTRADIÇÃO e falha; linha sem marca de merge continua
# fora do check. Se nenhum arquivo coberto existir, FALHA em vez de passar
# em silêncio.
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
errors = []
checked = 0
for doc in covered:
    try:
        rel = doc.relative_to(root).as_posix()
    except ValueError:
        rel = doc.name
    for line_no, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
        is_merged = bool(re.search(r"merged|mesclado", line, re.IGNORECASE)) \
            or line.lstrip().startswith("- [x]")
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
