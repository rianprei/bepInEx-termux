#!/usr/bin/env python3
"""Nenhum teste pode existir no disco sem destino declarado no gate.

O QUE ESTE CHECK FAZ, E POR QUE ELE EXISTE. O repositório tinha 27 arquivos de
teste versionados e o `verify_all.sh` executava 17 deles. Os outros dez
existiam, eram linted (`bash -n` e `shellcheck` passam) e nunca rodavam: um
teste que ninguém executa é uma opinião sobre o código, e a opinião envelhece
sem que ninguém perceba. Foi assim que dois testes de extração de tarball com
symlink, link e hardlink ficaram meses versionados e lintados sem nunca terem
rodado uma vez.

A REGRA, em uma linha. Todo arquivo de teste no disco tem exatamente uma linha
em `tools/gate_tests.list`, e a linha diz o destino dele:

  run    o verify_all executa o arquivo, por essa lista, e a etapa falha se o
         teste falhar;
  wired  o arquivo já é executado, por uma etapa do verify_all.sh ou por outro
         teste do manifesto — e o check CONFERE a afirmação, procurando o nome
         do arquivo no verify_all.sh ou em outro teste do manifesto; se a prova
         não existe, FAIL, porque "wired" sem prova é o mesmo buraco com um
         nome mais bonito;
  skip   o arquivo não roda no gate, com o motivo obrigatório, e o motivo sai
         impresso em toda execução do gate.

Por que o motivo importa tanto quanto o modo. Um teste desligado sem motivo é
indistinguível de um teste esquecido, e é por isso que a lista de exceção é
impressa: desligar passa a ser um ato visível, com o conserto anotado, e não um
sumiço. O motivo tem tamanho mínimo porque "não roda aqui" não é motivo, é
sintoma.

A VARREDURA É POR SISTEMA DE ARQUIVOS, E NÃO POR `git ls-files`. Um teste novo
precisa ser julgado no minuto em que existe, versionado ou não — a mesma razão
que fez o check de contagem literal largar o índice do git.

O QUE ESTE CHECK NÃO FAZ. Ele não roda os testes: ele julga o destino
declarado. Quem prova que um teste `run` rodou é a tabela do próprio gate, que
tem uma linha por entrada. A garantia que este arquivo dá é outra, e é a que
importa: nenhum arquivo de teste fica sem destino.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
MANIFEST = ROOT / "tools/gate_tests.list"
VERIFY_ALL = ROOT / "tools/verify_all.sh"
TEST_ROOT = ROOT / "test"
SUFFIXES = (".sh", ".py")
# Motivo mínimo da exceção. Abaixo disso não é motivo, é desculpa; e "não roda
# aqui" sem o conserto é exatamente o que este check existe para impedir.
MIN_REASON = 30
# fixtures/ são dados de entrada dos testes, não testes.
SKIP_DIRS = ("fixtures", "__pycache__")


def die(messages):
    print("\n".join(messages), file=sys.stderr)
    return 1


def discover() -> list[str]:
    """Arquivos de teste do disco, relativos à raiz, ordenados."""
    found = []
    for dirpath, dirnames, filenames in os.walk(TEST_ROOT):
        rel_dir = Path(dirpath).relative_to(ROOT)
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        if any(part in SKIP_DIRS for part in rel_dir.parts):
            continue
        for name in sorted(filenames):
            if name.endswith(SUFFIXES):
                found.append(str((rel_dir / name).as_posix()))
    return sorted(found)


def parse_manifest() -> list[tuple[str, str, str, int]]:
    entries = []
    for line_no, raw in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in raw.split("\t")]
        if len(parts) == 2:
            path, mode, reason = parts[0], parts[1], ""
        elif len(parts) >= 3:
            path, mode, reason = parts[0], parts[1], "\t".join(parts[2:])
        else:
            path, mode, reason = raw.strip(), "", ""
        entries.append((path, mode, reason, line_no))
    return entries


def main() -> int:
    if not MANIFEST.is_file():
        return die([f"{MANIFEST.relative_to(ROOT)}: manifesto ausente; sem ele nenhum "
                    f"teste pode ser ligado, e a lista de excecao tambem"])
    disk = set(discover())
    entries = parse_manifest()
    errors: list[str] = []

    if not entries:
        return die([f"{MANIFEST.relative_to(ROOT)}: manifesto sem nenhuma entrada; "
                    f"{len(disk)} arquivos de teste no disco estao sem destino"])

    seen: dict[str, int] = {}
    for path, mode, reason, line_no in entries:
        if path in seen:
            errors.append(
                f"tools/gate_tests.list:{line_no}: {path} ja foi declarado na "
                f"linha {seen[path]}; um teste tem um destino, nao dois"
            )
        else:
            seen[path] = line_no
        if mode not in ("run", "wired", "skip"):
            errors.append(
                f"tools/gate_tests.list:{line_no}: modo {mode!r} invalido em {path}; "
                f"use run, wired ou skip"
            )
        if not (ROOT / path).is_file():
            errors.append(
                f"tools/gate_tests.list:{line_no}: {path} esta no manifesto e nao "
                f"existe no disco; destino de arquivo apagado"
            )
        if mode == "skip" and len(reason) < MIN_REASON:
            errors.append(
                f"tools/gate_tests.list:{line_no}: excecao de {path} sem motivo "
                f"({len(reason)} de {MIN_REASON} caracteres). Desligar teste e "
                f"decisao escrita com conserto anotado, nao sumico."
            )

    # Nenhum arquivo de teste pode ficar de fora do manifesto. Este e o buraco
    # original: teste novo, linted, nunca executado, e ninguem percebe.
    for path in sorted(disk - set(seen)):
        # `path` já é relativo à raiz ("test/x.sh"). Prefixar "test/" de novo
        # produzia "test/test/x.sh": um caminho que não existe, e a mensagem
        # punha o autor a procurar o arquivo errado. O nome tem que ser o
        # caminho REAL, porque é nele que o autor vai olhar.
        errors.append(
            f"{path}: arquivo de teste no disco sem linha em "
            f"tools/gate_tests.list — ou ele roda (run/wired), ou vira excecao "
            f"escrita (skip com motivo). Sem isso o gate nao sabe o que ele prova."
        )
    for path in sorted(set(seen) - disk):
        errors.append(f"tools/gate_tests.list: {path} nao existe mais no disco")

    # Prova do modo wired: o nome do arquivo tem que aparecer no verify_all.sh ou
    # em outro arquivo do manifesto. Sem a prova, wired vira sinônimo de skip.
    verify_text = VERIFY_ALL.read_text(encoding="utf-8") if VERIFY_ALL.is_file() else ""
    bodies: dict[str, str] = {}
    for path, mode, _reason, _line in entries:
        if mode == "wired" and (ROOT / path).is_file():
            try:
                bodies[path] = (ROOT / path).read_text(encoding="utf-8", errors="replace")
            except OSError:
                bodies[path] = ""

    def proves(path: str, seen_wired: set[str]) -> bool:
        if Path(path).name in verify_text:
            return True
        for other, body in bodies.items():
            if other != path and Path(path).name in body and other in seen_wired:
                return True
        return False

    # O manifesto tem que ser lido ANTES da primeira etapa do gate. Não é
    # estética: a primeira linha do manifesto é o check da ligação, e o texto
    # (deste arquivo, do manifesto e do bloco no verify_all.sh) promete que ele
    # "roda antes de tudo". Prometer em comentário e fazer depois é a variante
    # mais barata de mentira — e foi o que a revisão pegou: o bloco vivia na
    # linha ~455, depois de dezenas de etapas. Aqui a promessa vira verificação.
    if VERIFY_ALL.is_file():
        gate_lines = VERIFY_ALL.read_text(encoding="utf-8").splitlines()
        block_at = next(
            (i for i, l in enumerate(gate_lines)
             if "O manifesto de testes do gate" in l), -1)
        first_step_at = next(
            (i for i, l in enumerate(gate_lines)
             if l.startswith("run_step \"") or l.startswith("run_ndk \"")), -1)
        if block_at < 0:
            errors.append(
                "tools/verify_all.sh: o bloco do manifesto (\"O manifesto de "
                "testes do gate\") nao existe; sem ele nenhum teste tem destino"
            )
        elif first_step_at >= 0 and block_at > first_step_at:
            errors.append(
                f"tools/verify_all.sh:{block_at + 1}: o manifesto e' lido "
                f"DEPOIS da primeira etapa (linha {first_step_at + 1}); a "
                f"primeira linha do manifesto e' o check da ligacao, entao o "
                f"texto que promete 'antes de tudo' so e verdade com o bloco "
                f"antes de qualquer run_step/run_ndk"
            )

    wired_seen: set[str] = set()
    pending = [p for p, m, _r, _l in entries if m == "wired"]
    for _ in range(len(pending) + 1):
        for path in pending:
            if path not in wired_seen and proves(path, wired_seen):
                wired_seen.add(path)

    for path, mode, _reason, line_no in entries:
        if mode == "wired" and path not in wired_seen:
            errors.append(
                f"tools/gate_tests.list:{line_no}: {path} declarado wired, mas o "
                f"nome dele nao aparece no verify_all.sh nem em outro teste "
                f"declarado; wired sem prova e skip sem motivo"
            )

    if errors:
        return die(errors)

    runs = [p for p, m, _r, _l in entries if m == "run"]
    wired = [p for p, m, _r, _l in entries if m == "wired"]
    skips = [(p, r) for p, m, r, _l in entries if m == "skip"]
    print(
        f"gate-wiring: {len(disk)} arquivos de teste no disco; "
        f"{len(runs)} executados por este manifesto, "
        f"{len(wired)} ja executados por etapa (com prova), "
        f"{len(skips)} excecoes"
    )
    for path, reason in skips:
        print(f"gate-wiring: excecao — {path}: {reason}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
