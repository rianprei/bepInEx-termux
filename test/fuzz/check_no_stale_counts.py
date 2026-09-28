#!/usr/bin/env python3
"""Nenhuma contagem literal sobre alvo/seed/binário nos arquivos de fuzz.

O PROBLEMA. A lista de alvos do gate vive em `TARGETS`/`RUNS_DEFAULT`, e o texto
ao redor dela repetia a quantidade por extenso. Cada alvo novo tornava a frase
errada em silêncio: o README dizia uma coisa, o gate executava outra, e ninguém
descobria porque o texto não é executado. Foi o que aconteceu com o alvo do
encoder de thunk, que entrou na lista e deixou para trás a contagem nos dois
arquivos que descrevem a etapa.

A REGRA. Nenhuma contagem por extenso nem por dígito pode modificar o
substantivo de alvo. O que dispara:

  - dígito + substantivo ("5 alvos", "40000 execs" só dispara se o substantivo
    for o de alvo — contagem de execs e de segundos é calibration, não lista);
  - número por extenso a partir de dois + substantivo, singular ou plural;
  - "um"/"uma" + substantivo PLURAL.

A ÚLTIMA REGRA é a distinção que evita o falso positivo que faria este check
inútil: em português "um" é artigo na maioria das frases ("um harness que morre
na primeira exec", "um binário de teste"). Tratá-lo sempre como contagem obrigaria
a reescrever prosa que não afirma nada sobre tamanho de lista, e o primeiro
desenvolvedor a tomar uma decision ruim seria desligar o check. Contagem
declarada é número escrito por extenso seguido de substantivo, ou por extenso a
partir de dois. "um" isolado não é contagem; "um alvos" seria erro de digitação
e também falha aqui.

O QUE NÃO É PROBLEMA. Contagem de execs, de segundos, de entradas de watch, de
linhas: são calibration medida, não lista que cresce. E `TARGETS`/`RUNS_DEFAULT`
são a fonte da verdade — o texto aponta para elas em vez de repeti-las.

Como o arquivo é varrido por ele mesmo, nenhuma frase aqui pode casar com o
próprio padrão: é por isso que o docstring acima descreve o padrão em prosa em
vez de colar o exemplo.
"""
import re
import subprocess
import sys
from pathlib import Path

# Substantivos de alvo. Aceita com e sem acento porque o repositório mistura as
# duas grafias nos mesmos arquivos.
NOUNS = (
    r"alvos?|targets?|sementes?|seeds?|harness(?:es)?|"
    r"bin[aá]rios?"
)
# Plural obrigatório: o que distingue contagem de artigo.
PLURAL_NOUNS = (
    r"alvos|targets|sementes|seeds|harness(?:es)|bin[aá]rios"
)
SPELLED = r"dois|duas|tr[eê]s|quatro|cinco|seis|sete|oito|nove|dez"

# "os"/"as" entre o número e o substantivo é o caso comum em tabela e prosa.
ARTICLE = r"(?:os|as)?"

DIGIT_RE = re.compile(rf"\b\d+\s+(?:{NOUNS})\b", re.IGNORECASE)
SPELLED_RE = re.compile(rf"\b(?:{SPELLED})\s+{ARTICLE}\s*(?:{NOUNS})\b", re.IGNORECASE)
ONE_PLURAL_RE = re.compile(rf"\b(?:um|uma)\s+(?:{PLURAL_NOUNS})\b", re.IGNORECASE)

# A checagem é do texto de fuzz: o diretório inteiro (README, runner, gerador de
# corpus, alvos, fixtures) e o runner do gate, que é onde a contagem vivia.
TARGETS_OF_SCAN = ["tools/verify_all.sh"]


def scanned_files(root: Path) -> list[Path]:
    tracked = subprocess.run(
        ["git", "ls-files", "test/fuzz", "tools/verify_all.sh"],
        cwd=root, capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    # Só texto: o corpus tem .so/binário de semente, e abrir como texto Erro.
    keep = (".md", ".sh", ".py", ".cpp", ".c", ".h", ".txt")
    return [root / item for item in tracked if item.endswith(keep)]


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors = []
    checked = 0
    for path in scanned_files(root):
        try:
            body = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for line_no, line in enumerate(body.splitlines(), 1):
            for pattern in (DIGIT_RE, SPELLED_RE, ONE_PLURAL_RE):
                for match in pattern.finditer(line):
                    errors.append(
                        f"{path.relative_to(root)}:{line_no}: contagem literal "
                        f"{match.group(0)!r} — a lista de alvos cresce e o texto "
                        f"não pode dizer quanta é; aponte para TARGETS/"
                        f"RUNS_DEFAULT"
                    )
        checked += 1
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"stale-counts: {checked} arquivos de fuzz sem contagem literal de alvo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
