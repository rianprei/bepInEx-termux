#!/usr/bin/env python3
"""Nenhuma contagem literal sobre alvo/seed/binário nos arquivos de fuzz.

O PROBLEMA. A lista de alvos do gate vive em `TARGETS`/`RUNS_DEFAULT`, e o texto
ao redor dela repetia a quantidade por extenso. Cada alvo novo tornava a frase
errada em silêncio: o README dizia uma coisa, o gate executava outra, e ninguém
descobria porque texto não é executado. Foi o que aconteceu com o alvo do
encoder de thunk, que entrou na lista e deixou para trás a contagem nos dois
arquivos que descrevem a etapa.

A REGRA, em prosa. Dispara quando um número — escrito em dígito ou por extenso,
em português ou em inglês — modifica o substantivo de alvo, isto é, quando os
dois aparecem juntos. O separador entre eles pode ser espaço, hífen ou
travessão, e entre o número e o substantivo cabe um artigo ou um qualificador.

A EXCEÇÃO DO "um", e por que ela existe. Em português "um" é artigo na maioria
das frases — a forma mais comum de dizer "um alvo" no texto é exatamente essa, e
não afirma nada sobre tamanho de lista. Tratá-lo sempre como contagem obrigaria
a reescrever prosa inocente, e o primeiro desenvolvedor a tomar uma decisão ruim
seria desligar este check; um check que força mentira é pior do que nenhum.
O que dispara é a forma que DECLARA a contagem: o "um" com um marcador de
quantidade em volta, seja antes, depois ou entre o número e o substantivo.
Marcadores são "só", "apenas", "sozinho", "único" e, em inglês, "only", "just" e
"single". A lista completa de número por extenso vai de zero até vinte e depois
as dezenas, até cem, nos dois idiomas.

O QUE NÃO É PROBLEMA. Contagem de execs, de segundos, de entradas de watch, de
linhas: é calibração medida, não lista que cresce. E `TARGETS`/`RUNS_DEFAULT`
são a fonte da verdade — o texto aponta para elas em vez de repeti-las.

POR QUE A VARREDURA É POR SISTEMA DE ARQUIVOS, E NÃO `git ls-files`. A versão
anterior deste check listava os arquivos versionados, e por isso o gate passou
verde com o próprio check ainda não versionado: o arquivo se autoexcluía da
varredura. O texto do check é parte do que ele vigia, então ele tem que se ver.
"""
import re
import sys
from pathlib import Path

# Substantivo de alvo. Aceita com e sem acento porque o repositório mistura as
# duas grafias nos mesmos arquivos. O agrupamento NÃO é opcional: sem ele a
# alternação vaza para a cauda do padrão inteiro e casa substantivo solto, sem
# número nenhum — o bug que o gate não pegou na primeira versão.
NOUN = (
    r"(?:alvos?|targets?|sementes?|seeds?|harness(?:es)?|"
    r"bin[aá]rios?|sanitizers?)"
)
# Plural: o que distingue contagem de artigo.
PLURAL_NOUN = r"alvos|targets|sementes|seeds|harness(?:es)|bin[aá]rios|sanitizers"

# Número por extenso, PT e EN. O zero entra de propósito: negar que há
# alvos é uma afirmação sobre a lista, e é a que mais envelhece.
PT_NUM = (
    r"zero|um|uma|dois|duas|tr[eê]s|quatro|cinco|seis|sete|oito|nove|dez|"
    r"onze|doze|treze|quatorze|catorze|quinze|"
    r"dezesseis|dezessete|dezoito|dezenove|vinte|"
    r"trinta|quarenta|cinquenta|sessenta|setenta|oitenta|noventa|cem"
)
EN_NUM = (
    r"zero|one|two|three|four|five|six|seven|eight|nine|ten|"
    r"eleven|twelve|thirteen|fourteen|fifteen|sixteen|seventeen|eighteen|"
    r"nineteen|twenty|thirty|forty|fifty|sixty|seventy|eighty|ninety|hundred"
)
NUMBER = rf"(?:{PT_NUM}|{EN_NUM})"

# Separador aceito entre número e substantivo: espaço (inclusive repetido),
# hífen e as duas travessões. Não pode ser vazio, senão "cincoalvos" contaria.
SEP_ONE = r"[\s\-‐-―]"
SEP = rf"{SEP_ONE}+"
# Entre número e substantivo cabem artigo e qualificador, e o separador depois
# do qualificador é opcional: no caso comum — número, um espaço, substantivo —
# há um só separador, e exigir dois faria o padrão não casar com a frase mais
# óbvia que ele existe para pegar.
MID = rf"{SEP}(?:os|as)?(?:(?:[uú]nic|unic|single)\w*)?{SEP_ONE}*"

# Marcador que transforma artigo em contagem declarada. O "só" precisa dos
# dois acentos na classe: com IGNORECASE o "o" do padrão não casa o "ó" do
# texto, e o marcador mais comum em português era justamente o que não casava.
MARK = r"(?:[sś][oó]|sozinh\w*|apenas|only|just|single|[uú]nic\w*|unic\w*)"

DIGIT_RE = re.compile(rf"\b\d+{MID}{NOUN}\b", re.IGNORECASE)
# Número por extenso a partir de dois: sempre contagem.
SPELLED_RE = re.compile(
    rf"\b(?:{NUMBER}){MID}{NOUN}\b", re.IGNORECASE
)
# O "um" sozinho é artigo; com marcador em volta é contagem. O marcador pode
# estar antes do número, entre o número e o substantivo, ou depois do
# substantivo — por isso a janela é lida nos dois lados do casamento.
ONE_RE = re.compile(
    rf"\b(?:um|uma|one){MID}{NOUN}\b", re.IGNORECASE
)
WINDOW = 16

# O que é varrido: o diretório de fuzz inteiro (README, runner, gerador de
# corpus, alvos, fixtures) e o runner do gate, onde a contagem vivia. Por
# filesystem, não por índice do git: um arquivo novo ainda não versionado
# precisa ser vigiado no mesmo minuto em que existe.
SCAN_ROOTS = ("test/fuzz",)
SCAN_FILES = ("tools/verify_all.sh",)
TEXT_SUFFIXES = (".md", ".sh", ".py", ".cpp", ".c", ".h", ".txt")


def scanned_files(root: Path) -> list[Path]:
    found: list[Path] = []
    for sub in SCAN_ROOTS:
        found.extend(p for p in (root / sub).rglob("*") if p.is_file())
    found.extend(root / item for item in SCAN_FILES)
    return sorted(p for p in found if p.suffix in TEXT_SUFFIXES)


def report(path: Path, root: Path, line_no: int, text: str, hint: str) -> str:
    return (
        f"{path.relative_to(root)}:{line_no}: contagem literal {text!r} — "
        f"a lista de alvos cresce e o texto não pode dizer quanta é; "
        f"aponte para TARGETS/RUNS_DEFAULT{hint}"
    )


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors: list[str] = []
    checked = 0
    for path in scanned_files(root):
        try:
            body = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        checked += 1
        for line_no, line in enumerate(body.splitlines(), 1):
            for match in DIGIT_RE.finditer(line):
                errors.append(report(path, root, line_no, match.group(0), ""))
            for match in SPELLED_RE.finditer(line):
                if ONE_RE.fullmatch(match.group(0).strip()):
                    # "um" solto: só é contagem se houver marcador por perto.
                    before = line[max(0, match.start() - WINDOW):match.start()]
                    after = line[match.end():match.end() + WINDOW]
                    gap = match.group(0)
                    if re.search(rf"\b{MARK}\b", before, re.IGNORECASE) \
                            or re.search(rf"\b{MARK}\b", after, re.IGNORECASE) \
                            or re.search(rf"{MARK}", gap, re.IGNORECASE):
                        errors.append(report(path, root, line_no, match.group(0), ""))
                    continue
                errors.append(report(path, root, line_no, match.group(0), ""))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"stale-counts: {checked} arquivos de fuzz sem contagem literal de alvo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
