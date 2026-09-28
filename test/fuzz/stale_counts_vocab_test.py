#!/usr/bin/env python3
"""O vocabulário do check de contagem é uma lista só, e ela se prova nos dois sentidos.

O QUE ESTE TESTE FAZ, E POR QUE ELE EXISTE. O check de contagem literal
(`check_no_stale_counts.py`) tem vocabulário fechado, e a lista `VOCABULARY` é a
fonte da verdade: os padrões saem dela, e nada de palavra está escrito à mão em
dois lugares. O perigo que sobra é o oposto do que se imagina — a lista cresce
(e alguém acrescenta um quantificador novo), e a documentação continua
descrevendo a lista antiga. Foi exatamente o achado da revisão: o código aceitava
`par de`, `pair of` e `couple of`, e o texto não citava nenhum dos três. Texto
que descreve lista sem ser conferido contra a lista é o mesmo problema do
começo, com um degrau a mais de distância do código.

POR QUE CONFERIR NOS DOIS SENTIDOS, E NÃO SÓ A DOCUMENTAÇÃO. Um teste que só
procura as palavras no texto passa com a lista mentindo sobre o próprio
comportamento: o item pode estar escrito nos dois lugares e não disparar nada.
Por isso cada item tem uma frase de prova montada a partir de si mesmo, e esta
seção exige que ela dispare — e que a frase dos itens declarados fora de escopo
permaneça em silêncio. Assim o teste morre nos dois defeitos: item sem
documento, e documento sem comportamento.

A LISTA VIVA, E NÃO UMA CÓPIA. O teste importa o módulo do check e lê a lista
dele. Se a lista mudar, o teste muda junto, sem passo de sincronização para
esquecer.

REGRA DE OURO DESTE ARQUIVO. Ele é varrido pelo próprio check, então não pode
escrever a frase que o check caça. Daí as frases de prova serem montadas em
tempo de execução a partir do rótulo do item e do substantivo, e nunca
escritas como literal.
"""
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_no_stale_counts as checker  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
README = ROOT / "test/fuzz/README.md"

# Substantivo das frases de prova. Vive aqui como literal, mas nunca na mesma
# linha que um número — as frases são montadas em tempo de execução.
NOUN_PT = "alvos"
NOUN_EN = "targets"


def norm(text: str) -> str:
    """Minúsculas, sem acento, espaços colapsados: o repositório mistura as
    duas grafias nos mesmos arquivos, e comparar sem normalizar transformaria
    isso em falso negativo."""
    plain = unicodedata.normalize("NFD", text)
    plain = "".join(ch for ch in plain if unicodedata.category(ch) != "Mn")
    return re.sub(r"\s+", " ", plain.lower())


def mentions(haystack: str, label: str) -> bool:
    """A rótulo aparece como palavra, não como pedaço de outra.

    Sem a fronteira, "dez" passaria por estar dentro de "dezena" — que é
    exatamente o tipo de verde mentiroso que este arquivo existe para caçar.
    """
    return re.search(rf"(?<!\w){re.escape(norm(label))}(?!\w)", norm(haystack)) is not None


def limites_section(readme: str) -> str | None:
    match = re.search(r"^## Limites[ \t]*\n(.*?)(?=^## |\Z)", readme, re.M | re.S)
    return match.group(1) if match else None


def probe(term) -> str:
    """Frase que prova o comportamento prometido pelo item."""
    noun = NOUN_EN if term.lang == "en" else NOUN_PT
    if term.group == "digito":
        return f"3 {noun}"
    if term.group == "marca":
        # O marcador só é contagem perto do artigo, e é isso que a frase prova.
        artigo = "one" if term.lang == "en" else "um"
        return f"{artigo} {term.label} {noun}"
    return f"{term.label} {noun}"


def main() -> int:
    doc = checker.__doc__ or ""
    # mesmo contrato do check: README ilegível é FAIL com arquivo e linha, nunca
    # exceção não tratada. Um traceback também reprovaria o gate, mas a causa
    # sumiria dentro do traceback, e a causa é o que se lê primeiro.
    try:
        readme = README.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        data = README.read_bytes()
        line = data.count(b"\n", 0, exc.start) + 1
        print(
            f"{README.relative_to(ROOT)}:{line}: byte nao-UTF8 "
            f"0x{data[exc.start]:02x} no offset {exc.start} — sem o texto do "
            f"README nao ha o que conferir contra a lista",
            file=sys.stderr,
        )
        return 1
    limites = limites_section(readme)
    errors: list[str] = []

    if not limites:
        errors.append(
            "test/fuzz/README.md: sem a seção '## Limites' — ela é onde o "
            "vocabulário coberto e o que fica fora de escopo estão escritos, e "
            "o teste é que impede a lista de crescer sem o texto acompanhar"
        )

    for term in checker.VOCABULARY:
        # 1. Documentação, nos dois lugares, para todo item da lista.
        if not mentions(doc, term.label):
            errors.append(
                f"check_no_stale_counts.py: docstring não cita {term.label!r} "
                f"(grupo {term.group}) — a lista é a fonte da verdade, e o texto "
                f"que a descreve não pode ser uma versão anterior dela"
            )
        if limites is not None and not mentions(limites, term.label):
            errors.append(
                f"test/fuzz/README.md: seção '## Limites' não cita "
                f"{term.label!r} (grupo {term.group})"
            )

        # 2. Fiação: item na lista é item no regex, não só item escrito.
        attr = checker.GROUP_ATTRS.get(term.group)
        if attr is not None and term.alt not in getattr(checker, attr, ""):
            errors.append(
                f"check_no_stale_counts.py: {term.label!r} está na lista do grupo "
                f"{term.group}, mas a alternativa não está em {attr}"
            )

        # 3. Comportamento, nos dois sentidos.
        achados = checker.find_stale(probe(term))
        if term.group == "fora" and achados:
            errors.append(
                f"check_no_stale_counts.py: {term.label!r} está declarado como "
                f"fora de escopo, mas {probe(term)!r} disparou: "
                f"{achados[0][0]!r}"
            )
        if term.group != "fora" and not achados:
            errors.append(
                f"check_no_stale_counts.py: {term.label!r} (grupo {term.group}) "
                f"está documentado como coberto, mas {probe(term)!r} não "
                f"dispara nenhum padrão"
            )

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(
        f"vocab-test: {len(checker.VOCABULARY)} itens de vocabulário citados no "
        f"docstring e em '## Limites', todos com o comportamento prometido"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
