#!/usr/bin/env bash
# test/release_notes_check.sh — CHANGELOG v0.5.0 cobre os merges da base.
#
# Regra sem ovo-galinha: um merge M está coberto se alguma linha da seção
# v0.5.0 cita M OU QUALQUER commit introduzido por M
# (git rev-list M^1..M, que cobre o 2º pai e o polvo inteiro). O autor cita
# o PRIMEIRO commit da própria branch — estável e já existente antes de ele
# escrever a linha — e o gate aceita. O que esta etapa exige:
#   1. todo merge first-parent desde o sync (hash no cabeçalho da seção)
#      tem linha na seção v0.5.0 (pelo hash do merge ou de qualquer commit
#      que ele introduziu);
#   2. nenhum item do Pendente já entrou na base (ponta de uni/<nome>
#      ancestral de HEAD);
#   3. toda linha de merge tem descrição (não só outro hash).
# Assim o próximo merge que esquecer o CHANGELOG quebra o gate na hora.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

python3 - "$ROOT" <<'PY'
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
changelog = root / "CHANGELOG.md"
if not changelog.is_file():
    print("release-notes-check: CHANGELOG.md ausente na raiz", file=sys.stderr)
    raise SystemExit(1)
lines = changelog.read_text(encoding="utf-8").splitlines()

# Seção v0.5.0: do cabeçalho até o próximo ## .
start = next((i for i, l in enumerate(lines) if l.startswith("## v0.5.0")), None)
if start is None:
    print("release-notes-check: seção ## v0.5.0 ausente", file=sys.stderr)
    raise SystemExit(1)
end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")),
           len(lines))
section = lines[start:end]

sync_match = re.search(r"sync do CHANGELOG\s+\(`?([0-9a-f]{7,40})`?\)",
                       "\n".join(section))
if not sync_match:
    print("release-notes-check: hash de sync ausente no cabeçalho da v0.5.0",
          file=sys.stderr)
    raise SystemExit(1)
sync = sync_match.group(1)

def git(*args):
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)

errors = []

# 3. linhas de merge têm descrição (e coleta dos hashes citados).
merge_line_re = re.compile(r"^-\s+`([0-9a-f]{7,40})`\s+[—-]\s*(.*)$")
cited = {}
for line_no, line in enumerate(lines[start:end], start + 1):
    m = merge_line_re.match(line.strip())
    if not m:
        continue
    h, desc = m.group(1), m.group(2).strip()
    cited[h] = line_no
    if not desc or re.fullmatch(r"[0-9a-f]{7,40}", desc):
        errors.append(f"CHANGELOG.md:{line_no}: linha de merge sem descrição: {h}")

# 1. todo merge first-parent desde o sync tem linha: vale o hash do merge
#    ou de qualquer commit introduzido por ele (rev-list M^1..M). O autor
#    cita o PRIMEIRO commit da própria branch, que já existe antes da linha.
log = git("log", "--format=%H %P", "--merges", "--first-parent",
          f"{sync}..HEAD")
if log.returncode != 0:
    print(f"release-notes-check: git log falhou para {sync}..HEAD", file=sys.stderr)
    raise SystemExit(1)
cited_short = {h[:7] for h in cited}
for entry in log.stdout.splitlines():
    parts = entry.split()
    merge = parts[0]
    acceptable = {merge[:7]}
    introduced = git("rev-list", f"{merge}^1..{merge}")
    if introduced.returncode == 0:
        acceptable |= {h[:7] for h in introduced.stdout.split()}
    if not (acceptable & cited_short):
        errors.append(f"CHANGELOG.md: merge {merge[:7]} sem linha na v0.5.0 "
                      f"(vale o merge ou qualquer commit introduzido por ele)")

# 2. nenhum Pendente já entrou na base.
in_pendente = False
for line_no, line in enumerate(lines[start:end], start + 1):
    s = line.strip()
    if s.startswith("### "):
        in_pendente = s == "### Pendente (não mergeado, sem hash de merge)"
        continue
    if not in_pendente:
        continue
    m = re.match(r"^-\s+([A-Za-z0-9_.-]+)", s)
    if not m:
        continue
    branch = f"uni/{m.group(1)}"
    rev = git("rev-parse", "--verify", "-q", branch)
    if rev.returncode != 0:
        continue
    tip = rev.stdout.strip()
    if git("merge-base", "--is-ancestor", tip, "HEAD").returncode == 0:
        errors.append(f"CHANGELOG.md:{line_no}: {branch} já entrou na base "
                      f"mas está no Pendente")

if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"release-notes-check: v0.5.0 cobre os merges desde {sync[:7]}; "
      f"Pendente fora da base; {len(cited)} linhas com descrição")
PY
