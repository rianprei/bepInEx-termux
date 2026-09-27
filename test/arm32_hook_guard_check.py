#!/usr/bin/env python3
"""Prova que o mod ARM32 SAI antes do DobbyHook.

ACHADO DA REVISÃO (uni/arm32, 5edfb41): sa2ammo e sa2content chamavam
DobbyHook sem nenhuma guarda de arquitetura. O mesmo código rodava no ARM32,
onde o hook nunca foi validado em aparelho — e hook errado em 32-bit fecha o
jogo. A política do projeto (u_patch, u_noads, u_frida) é recusar com log e
sair limpo.

Este check é ESTÁTICO de propósito, e é o que pega a regressão: um teste
funct ional precisaria de aparelho ARM32, e um teste de "o hook não foi
chamado" em AArch64 passaria mesmo com a guarda ausente. O que se prova aqui é
a ORDEM no código: no texto do `.so` ARM32, o `#if !defined(__aarch64__)` tem
que vir ANTES de qualquer chamada a DobbyHook, e o `return` do caminho 32-bit
tem que estar dentro do bloco.

Por que texto e nao assembly: o binário ARM32 é compilado com a guarda
ATIVA, então o DobbyHook nem existe no código de máquina — a prova de que o
return acontece antes dele é a ordem no fonte. E o fonte é o que um
refactor pode quebrar em silêncio, que é o que a gente quer pegar.

Uso:  arm32_hook_guard_check.py <repo-raiz>
"""
import os
import re
import sys

# Mods que passam por DobbyHook, e o que cada um tem que ter.
#   guarda:  o #if !defined(__aarch64__) tem que existir e vir antes do hook
#   recusa:  o caminho 32-bit tem que RETORNAR (nao so logar e seguir)
# (file, nome, nº de DobbyHook) — a guarda de COMPILACAO e exigida de quem tem
# hook. u_frida NAO entra aqui: a guarda dele e em RUNTIME
# (uf_gadget_matches_process_abi), porque o frida-gadget.bin e um binario
# EXTERNO que pode ser de qualquer ABI — um #if de compilacao nao diria nada
# sobre o gadget que vai ser aberto. Mecanismo diferente, exigencia diferente.
ESPERADO = {
    "mods/sa2ammo/jni/sa2ammo_mod.cpp": ("sa2ammo", 2),
    "mods/sa2content/jni/sa2content_mod.cpp": ("sa2content", 2),
    "mods/u_noads/jni/u_noads_mod.cpp": ("u_noads", 1),
    "mods/u_patch/jni/u_patch_mod.cpp": ("u_patch", 2),
}

# Quem recusa em RUNTIME, e o que tem que existir.
RUNTIME = {
    "mods/u_frida/jni/u_frida_mod.cpp": ("u_frida", "uf_gadget_matches_process_abi"),
}

GUARDA_RE = re.compile(r"#\s*if\s*!\s*defined\(__aarch64__\)")
DOBBY_RE = re.compile(r"\bDobbyHook\s*\(")
RETURN_RE = re.compile(r"\breturn\b")


def _corpo_da_funcao(texto, nome):
    """(inicio, fim) do corpo de uma funcao, pelo nome. Heuristica de chaves.

    Serve para os dois formatos que existem no projeto:
      - o DobbyHook DENTRO do proprio worker (sa2ammo, sa2content);
      - o DobbyHook numa FUNCAO AUXILIAR chamada pelo construtor depois da
        guarda (u_noads, u_patch). Aí a ordem textual do fonte engana — o
        helper é definido antes do construtor, mas só é CHAMADO depois da
        guarda, e o que roda em 32-bit é o caminho de execução, não a
        posição no texto.
    """
    m = re.search(r"\n[a-zA-Z_][\w:<>\* ]*\b" + re.escape(nome) + r"\s*\([^;{]*\)\s*\{", texto)
    if m is None:
        return None
    abre = texto.index("{", m.end() - 1)
    prof, k = 0, abre
    while k < len(texto):
        if texto[k] == "{":
            prof += 1
        elif texto[k] == "}":
            prof -= 1
            if prof == 0:
                return (abre, k)
        k += 1
    return (abre, len(texto))


def _funcoes_com_dobby(texto):
    """Nomes das funcoes cujo corpo tem DobbyHook."""
    nomes = []
    for m in re.finditer(r"\n[a-zA-Z_][\w:<>\* ]*?\b(\w+)\s*\([^;{]*\)\s*\{", texto):
        ini, fim = _corpo_da_funcao(texto, m.group(1))
        if ini is not None and DOBBY_RE.search(texto[ini:fim]):
            nomes.append(m.group(1))
    return nomes


def check_modo32_sai_antes_do_hook(path, texto, falhas):
    """O caminho 32-bit tem que RETORNAR antes de alcancar qualquer DobbyHook."""
    guardas = [m.start() for m in GUARDA_RE.finditer(texto)]
    if not guardas:
        falhas.append(f"{path}: sem guarda #if !defined(__aarch64__)")
        return
    primeira = guardas[0]
    fecha = texto.find("#endif", primeira)
    if fecha == -1:
        falhas.append(f"{path}: a guarda nao tem #endif")
        return
    if not RETURN_RE.search(texto[primeira:fecha]):
        falhas.append(f"{path}: o bloco 32-bit nao tem return — ele loga e segue")
        return

    # Todo hook esta numa funcao (a propria ou uma auxiliar). A guarda tem que
    # estar ANTES do ponto onde o caminho de entrada a alcanca.
    alcancavel = []
    for nome in _funcoes_com_dobby(texto):
        corpo = _corpo_da_funcao(texto, nome)
        if corpo is None:
            continue
        if corpo[0] <= primeira < corpo[1]:
            # o hook esta na propria funcao de entrada: exige ordem
            h = DOBBY_RE.search(texto, corpo[0], corpo[1])
            if h and h.start() < fecha:
                falhas.append(
                    f"{path}: DobbyHook (linha {texto[:h.start()].count(chr(10)) + 1}) "
                    f"DENTRO do bloco 32-bit de {nome}")
            continue
        # hook em auxiliar: a guarda tem que vir antes da PRIMEIRA chamada a
        # essa auxiliar dentro do caminho de entrada.
        chamada = re.search(r"\b" + re.escape(nome) + r"\s*\(", texto)
        if chamada and chamada.start() > primeira:
            alcancavel.append((nome, chamada.start()))

    for nome, pos in alcancavel:
        if pos < fecha:
            falhas.append(
                f"{path}: {nome}() (que tem DobbyHook) e chamada na linha "
                f"{texto[:pos].count(chr(10)) + 1}, DENTRO do caminho 32-bit")


def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else "."
    falhas = []
    for rel, (nome, n_hooks) in sorted(ESPERADO.items()):
        caminho = os.path.join(raiz, rel)
        if not os.path.isfile(caminho):
            falhas.append(f"{rel}: arquivo ausente")
            continue
        texto = open(caminho, encoding="utf-8").read()
        n_real = len([m for m in DOBBY_RE.finditer(texto)])
        if n_real != n_hooks:
            falhas.append(
                f"{rel}: {n_real} DobbyHook no fonte, esperava {n_hooks}")
            continue
        check_modo32_sai_antes_do_hook(rel, texto, falhas)
        print(f"  [OK] {nome}: guarda 32-bit antes de {n_real} DobbyHook, e retorna")

    for rel, (nome, simbolo) in sorted(RUNTIME.items()):
        caminho = os.path.join(raiz, rel)
        if not os.path.isfile(caminho):
            falhas.append(f"{rel}: arquivo ausente")
            continue
        texto = open(caminho, encoding="utf-8").read()
        if simbolo not in texto:
            falhas.append(f"{rel}: sem a guarda de ABI em runtime ({simbolo})")
        else:
            print(f"  [OK] {nome}: guarda de ABI em runtime ({simbolo})")

    if falhas:
        for f in falhas:
            print(f"  [FAIL] {f}")
        print("arm32_hook_guard: HOUVE FALHAS")
        return 1
    print("arm32_hook_guard: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
