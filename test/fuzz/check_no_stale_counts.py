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
Por isso o artigo sozinho passa. Disparam as duas formas que declaram a
contagem. A primeira é o artigo com marcador de quantidade em volta, seja
antes, depois ou entre o número e o substantivo; os marcadores são "só",
"apenas", "sozinho", "único" e, em inglês, "only", "just" e "single". A segunda
é o artigo antes de substantivo no plural: em português isso não é artigo, é
erro de digitação ou a contagem que entrou junto com a troca do substantivo, e
falha mesmo sem marcador por perto.

O VOCABULÁRIO FECHADO, E ONDE ELE VIVE DE FATO. Ele é a lista `VOCABULARY` mais
abaixo, e os quatro padrões deste arquivo (dígito, número PT, número EN,
quantificador, marcador) são montados a partir dela — não há palavra escrita à
mão em dois lugares. Este texto e a seção "## Limites" do README são conferidos
contra essa lista pelo `test/fuzz/stale_counts_vocab_test.py`, que falha se um
item da lista não estiver escrito nos dois, e também se um item escrito não se
comportar como a lista promete. A lista inteira, por extenso: dígito. Em
português, zero, um, uma, dois, duas, três, quatro, cinco, seis, sete, oito,
nove, dez, onze, doze, treze, quatorze, catorze, quinze, dezesseis, dezessete,
dezoito, dezenove, vinte, trinta, quarenta, cinquenta, sessenta, setenta,
oitenta, noventa, cem. Em inglês, zero, one, two, three, four, five, six,
seven, eight, nine, ten, eleven, twelve, thirteen, fourteen, fifteen, sixteen,
seventeen, eighteen, nineteen, twenty, thirty, forty, fifty, sixty, seventy,
eighty, ninety, hundred. Quantificadores: dúzia, dezenas, dozens, par de,
pair of, couple of, meia dúzia, half a dozen. Marcadores: só, sozinho, apenas,
only, just, single, único. E a regra do artigo antes de substantivo no plural.
A palavra é aceita sem acento também (três e tres, dúzia e duzia). Nada mais
dispara, e acrescentar palavra é decisão documentada na lista, nunca efeito
colateral de arrumar outra coisa.

O QUE ESTÁ DECLARADAMENTE FORA DE ESCOPO, e POR QUÊ. "vários", "alguns", "some" e
"several" ficam de fora porque não afirmam número nenhum: dizem que há mais de
um sem dizer quantos, e por isso não envelhecem quando a lista cresce — é a
diferença entre uma afirmação que a lista pode desmentir e uma que ela não
toca. "integrado", "done" e "landed" também ficam fora: são sinônimos de merge
que este gate não promete conferir, e a lista de marcas está escrita no gate de
hash, que é quem a mantém.

Por que o gate não tenta ser completo em linguagem natural: a invariante que
importa — a lista de alvos e a tabela do README não divergirem nos dois
sentidos — é estrutural e está verificada no gate. Este check é a rede de
proteção do texto corrido em volta dela, e uma rede com vocabulário escrito
vale mais do que uma heurística que nunca termina.

O QUE NÃO É PROBLEMA. Contagem de execs, de segundos, de entradas de watch, de
linhas: é calibração medida, não lista que cresce. E `TARGETS`/`RUNS_DEFAULT`
são a fonte da verdade — o texto aponta para elas em vez de repeti-las.

POR QUE A VARREDURA É POR SISTEMA DE ARQUIVOS, E NÃO `git ls-files`. A versão
anterior deste check listava os arquivos versionados, e por isso o gate passou
verde com o próprio check ainda não versionado: o arquivo se autoexcluía da
varredura. O texto do check é parte do que ele vigia, então ele tem que se ver.

E POR QUE ARQUIVO ILEGÍVEL É FAIL, E NÃO PULO. Ler bytes com `errors="ignore"`
e seguir em frente é o modo mais barato de deixar este check decorativo: um
arquivo salvo na codificação errada — ou em que alguém escreveu a contagem em
cima de um texto que não é texto — sai da varredura sem que ninguém veja nada.
Então o byte que não decodifica em UTF-8 é erro, com arquivo e linha, e a linha
é a primeira depois do byte quebrado; o arquivo não entra na contagem de
arquivos lidos, e o gate reprova. É o mesmo motivo do "sem sanitizer é FAIL": um
check que reporta verde sem ter lido não está mentindo por acidente, está
mentindo por construção.
"""
import re
import sys
from collections import namedtuple
from pathlib import Path

# ── O VOCABULÁRIO: UMA LISTA SÓ, E OS PADRÕES SAEM DELA ─────────────────────
# Cada item é uma palavra (ou a forma como ela aparece escrita) que este check
# reconhece. `alt` é a alternativa de regex que o item contribui; quando fica
# vazia, a própria palavra é a alternativa. O grupo "fora" marca o que NÃO
# dispara: o item existe para a documentação citar a palavra e para o teste
# garantir que ela continua em silêncio depois de alguém mexer no resto.
# `lang` é só para o teste montar a frase de prova ("alvos" ou "targets").
Term = namedtuple("Term", "label alt group lang")


def V(label, group, alt=None, lang="pt"):
    if alt is None and group != "fora":
        alt = label
    return Term(label, alt, group, lang)


VOCABULARY = (
    # Número por extenso, em português. "um" e "uma" entram mesmo sendo artigo
    # na maioria das frases: a exceção é aplicada no casamento, não na lista — se
    # saíssem daqui, a lista diria que não são número, e o texto do README
    # passaria a discordar dela. "três" carrega o acento como classe porque o
    # repositório mistura as duas grafias nos mesmos arquivos.
    V("zero", "pt_num"),
    V("um", "pt_num"),
    V("uma", "pt_num"),
    V("dois", "pt_num"),
    V("duas", "pt_num"),
    V("três", "pt_num", r"tr[eê]s"),
    V("quatro", "pt_num"),
    V("cinco", "pt_num"),
    V("seis", "pt_num"),
    V("sete", "pt_num"),
    V("oito", "pt_num"),
    V("nove", "pt_num"),
    V("dez", "pt_num"),
    V("onze", "pt_num"),
    V("doze", "pt_num"),
    V("treze", "pt_num"),
    V("quatorze", "pt_num"),
    V("catorze", "pt_num"),
    V("quinze", "pt_num"),
    V("dezesseis", "pt_num"),
    V("dezessete", "pt_num"),
    V("dezoito", "pt_num"),
    V("dezenove", "pt_num"),
    V("vinte", "pt_num"),
    V("trinta", "pt_num"),
    V("quarenta", "pt_num"),
    V("cinquenta", "pt_num"),
    V("sessenta", "pt_num"),
    V("setenta", "pt_num"),
    V("oitenta", "pt_num"),
    V("noventa", "pt_num"),
    V("cem", "pt_num"),
    # Número por extenso, em inglês. O README do gate escreve em português e o
    # código de terceiros em inglês: as duas listas precisam fechá-lo.
    V("zero", "en_num", lang="en"),
    V("one", "en_num", lang="en"),
    V("two", "en_num", lang="en"),
    V("three", "en_num", lang="en"),
    V("four", "en_num", lang="en"),
    V("five", "en_num", lang="en"),
    V("six", "en_num", lang="en"),
    V("seven", "en_num", lang="en"),
    V("eight", "en_num", lang="en"),
    V("nine", "en_num", lang="en"),
    V("ten", "en_num", lang="en"),
    V("eleven", "en_num", lang="en"),
    V("twelve", "en_num", lang="en"),
    V("thirteen", "en_num", lang="en"),
    V("fourteen", "en_num", lang="en"),
    V("fifteen", "en_num", lang="en"),
    V("sixteen", "en_num", lang="en"),
    V("seventeen", "en_num", lang="en"),
    V("eighteen", "en_num", lang="en"),
    V("nineteen", "en_num", lang="en"),
    V("twenty", "en_num", lang="en"),
    V("thirty", "en_num", lang="en"),
    V("forty", "en_num", lang="en"),
    V("fifty", "en_num", lang="en"),
    V("sixty", "en_num", lang="en"),
    V("seventy", "en_num", lang="en"),
    V("eighty", "en_num", lang="en"),
    V("ninety", "en_num", lang="en"),
    V("hundred", "en_num", lang="en"),
    # Dígito: o item mais simples da lista, e o único que não é palavra.
    V("dígito", "digito", r"\d+"),
    # Quantificador numérico: não é número por extenso e nomeia uma quantidade
    # do mesmo jeito. A preposição vem dentro da palavra do item ("par de",
    # "couple of"), e para os demais o QUANT_MID abaixo a torna opcional — por
    # isso a meia dúzia casa com e sem a preposição antes do substantivo.
    # E note que escrever a frase proibida aqui, ainda que em comentário, é
    # fail: o check varre o próprio texto, sem exceção para quem o mantém.
    V("dúzia", "quant", r"d[uú]zias?"),
    V("dezenas", "quant", "dezenas?"),
    V("dozens", "quant", "dozens?", lang="en"),
    V("par de", "quant", r"par\s+de"),
    V("pair of", "quant", r"pair\s+of", lang="en"),
    V("couple of", "quant", r"couple\s+of", lang="en"),
    V("meia dúzia", "quant", r"meia\s+d[uú]zia"),
    V("half a dozen", "quant", r"half\s+a\s+dozen", lang="en"),
    # Marcador que transforma artigo em contagem declarada. O "só" precisa dos
    # dois acentos na classe: com IGNORECASE o "o" do padrão não casa o "ó" do
    # texto, e o marcador mais comum em português era justamente o que não casava.
    # As formas com sufixo vêm antes das curtas porque a alternação é ordenada:
    # com "só" na frente, "sozinho" casaria o "so" e o resto da frase passaria
    # fora. OPython refaz a tentativa se a frase não fechar depois, mas vale
    # não depender disso.
    V("sozinho", "marca", r"sozinh\w*"),
    V("único", "marca", r"[uú]nic\w*"),
    V("só", "marca", r"[sś][oó]"),
    V("apenas", "marca"),
    V("only", "marca", lang="en"),
    V("just", "marca", lang="en"),
    V("single", "marca", lang="en"),
    # Fora de escopo, por decisão escrita (ver o docstring). `alt` fica vazio de
    # propósito: o item não participa de alternação nenhuma.
    V("vários", "fora"),
    V("alguns", "fora"),
    V("some", "fora", lang="en"),
    V("several", "fora", lang="en"),
    V("integrado", "fora"),
    V("done", "fora", lang="en"),
    V("landed", "fora", lang="en"),
)

# Grupo -> atributo onde a alternativa dele aparece. O teste de vocabulário usa
# este mapa para provar que item na lista é item no regex, e não só item escrito.
GROUP_ATTRS = {
    "digito": "DIGIT",
    "pt_num": "PT_NUM",
    "en_num": "EN_NUM",
    "quant": "QUANT",
    "marca": "MARK",
}


def alts(group: str) -> str:
    found = [t.alt for t in VOCABULARY if t.group == group]
    if not found:
        raise SystemExit(f"check_no_stale_counts: grupo {group} sem item")
    return "|".join(found)


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

DIGIT = alts("digito")
# Número por extenso, PT e EN. O zero entra de propósito: negar que há
# alvos é uma afirmação sobre a lista, e é a que mais envelhece.
PT_NUM = alts("pt_num")
EN_NUM = alts("en_num")
NUMBER = rf"(?:{PT_NUM}|{EN_NUM})"

# Separador aceito entre número e substantivo: espaço (inclusive repetido),
# hífen e as duas travessões. Não pode ser vazio, senão "cincoalvos" contaria.
SEP_ONE = r"[\s\-‐-―]"
SEP = rf"{SEP_ONE}+"
# Quantificador numérico: a lista o define inteiro (ver VOCABULARY). PT e EN
# juntos porque o README do gate escreve em português e o código de terceiros
# em inglês.
QUANT = alts("quant")

# Marcador que transforma artigo em contagem declarada (lista em VOCABULARY).
# Fica declarado aqui, e não junto de MID, porque MID precisa dele: o marcador
# pode vir entre o número e o substantivo, e essa posição é a que o texto
# promete e o regex antigo não cumpria — com o marcador de duas letras ele
# casava, com o de uma letra não, sem diferença de significado entre as duas
# frases. Nenhum teste reclamava disso, porque a documentação não era conferida
# contra a lista; a lista mora aqui desde a versão anterior e é a mesma.
MARK_BODY = alts("marca")
MARK = rf"(?:{MARK_BODY})"

# Entre número e substantivo cabem artigo e marcador de quantidade, e o
# separador depois deles é opcional: no caso comum — número, um espaço,
# substantivo — há um só separador, e exigir dois faria o padrão não casar com a
# frase mais óbvia que ele existe para pegar.
MID = rf"{SEP}(?:os|as)?(?:{MARK_BODY})?{SEP_ONE}*"

DIGIT_RE = re.compile(rf"\b{DIGIT}{MID}{NOUN}\b", re.IGNORECASE)
# Quantificador numerico. A barra da tabela markdown fica entre a celula do
# rotulo e a do substantivo, e MID nao aceita barra, entao o padrao casa na
# mesma celula sem depender do pipe: e por isso que a varrura de linha inteira
# funciona para tabela sem caso especial.
# O quantificador leva preposicao antes do substantivo, e MID nao tem porque
# MID nao existe para numero: e a unica diferenca entre este casamento e o
# do numero.
QUANT_MID = rf"{SEP}(?:(?:d[eo]s?|of|to)\s+)?{SEP_ONE}*"
QUANT_RE = re.compile(rf"\b(?:{QUANT}){QUANT_MID}{NOUN}\b", re.IGNORECASE)
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
# Artigo seguido de substantivo no plural: o casamento existe para pegar a
# contagem que entrou junto com a troca do substantivo.
PLURAL_TAIL_RE = re.compile(rf"{PLURAL_NOUN}\b", re.IGNORECASE)
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


def find_stale(line: str) -> list[tuple[str, str]]:
    """Casamentos de contagem literal numa linha, como (texto, dica).

    Extraído do laço de main() porque o teste de vocabulário precisa exercitar
    exatamente este código, e não uma cópia dele: cópia de regra é regra que
    envelhece sozinha.
    """
    found: list[tuple[str, str]] = []
    for match in DIGIT_RE.finditer(line):
        found.append((match.group(0), ""))
    for match in QUANT_RE.finditer(line):
        found.append((match.group(0), ""))
    for match in SPELLED_RE.finditer(line):
        if ONE_RE.fullmatch(match.group(0).strip()):
            # "um" solto: só é contagem se houver marcador por perto.
            before = line[max(0, match.start() - WINDOW):match.start()]
            after = line[match.end():match.end() + WINDOW]
            gap = match.group(0)
            if re.search(rf"\b{MARK}\b", before, re.IGNORECASE) \
                    or re.search(rf"\b{MARK}\b", after, re.IGNORECASE) \
                    or re.search(rf"{MARK}", gap, re.IGNORECASE):
                found.append((match.group(0), ""))
                continue
            # Artigo no plural não é artigo: é erro de digitação que
            # nasceu de alguém substituindo o substantivo e o número
            # junto, e é a forma como a contagem entra sorrindo. Sem
            # esta regra a frase passa em silêncio — foi a regressão
            # apontada na revisão da versão reescrita.
            if PLURAL_TAIL_RE.search(gap):
                found.append((
                    match.group(0),
                    "; substantivo no plural apos artigo: contagem ou erro de digitação",
                ))
            continue
        found.append((match.group(0), ""))
    return found


def decoded_lines(path: Path) -> tuple[list[str], int | None, str | None]:
    """(linhas, linha_do_erro, motivo). Erro de decodificação volta como dado,
    nunca como exceção para o chamador silenciar."""
    data = path.read_bytes()
    try:
        return data.decode("utf-8").splitlines(), None, None
    except UnicodeDecodeError as exc:
        line = data.count(b"\n", 0, exc.start) + 1
        return [], line, f"byte nao-UTF8 0x{data[exc.start]:02x} no offset {exc.start}"


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors: list[str] = []
    checked = 0
    for path in scanned_files(root):
        rel = path.relative_to(root)
        try:
            lines, bad_line, why = decoded_lines(path)
        except OSError as exc:
            errors.append(f"{rel}: erro de leitura: {exc}")
            continue
        if bad_line is not None:
            errors.append(
                f"{rel}:{bad_line}: {why} — este check nao pula arquivo "
                f"ilegivel: e exatamente em texto que nao decodifica que a "
                f"contagem se esconde"
            )
            continue
        checked += 1
        for line_no, line in enumerate(lines, 1):
            for text, hint in find_stale(line):
                errors.append(report(path, root, line_no, text, hint))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"stale-counts: {checked} arquivos de fuzz sem contagem literal de alvo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
