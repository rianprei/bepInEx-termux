#!/usr/bin/env python3
"""Emissor push_mod: anuncia SIZE e envia exatamente SIZE bytes.

Uso: push_mod_emit.py <socket> <arquivo> <nome> <size>
  <socket>   endereço do companion: @nome (namespace abstrato, no device)
             ou caminho de socket unix no filesystem (testes de host).
  <arquivo>  o .so já stripado a enviar.
  <nome>     nome do mod no protocolo (ex.: 02_kungfux.so).
  <size>     tamanho esperado em bytes.

O arquivo é lido UMA vez; o SIZE anunciado é o len() do que foi lido e
conferido contra o esperado. SIZE anunciado == bytes enviados por
construção: se o arquivo mudou/encurtou entre o stat do chamador e o
envio (TOCTOU, adb push truncado), o emissor recusa com erro limpo em
vez de mandar um payload que não bate com o anúncio — o companion
escreveria um .so parcial como se fosse inteiro.
"""

import os
import socket
import sys

# Tempo de espera do socket. O cliente do Termux usava 20s; o emissor, antes
# desta refatoracao, usava o padrao do sistema. Fica 20s para que os dois
# caminhos esperem o mesmo antes de desistir.
SOCKET_TIMEOUT = 20.0


class EmitError(Exception):
    """Falha de emissao, com mensagem ja pronta para o usuario.

    Antes desta refatoracao a logica vivia dentro de main() e chamava die(),
    que faz sys.exit(1). Isso servia para o uso em linha de comando, mas
    impossibilitava o cliente do Termux REUSAR a mesma logica: importar o
    modulo e chamar main() matava o processo inteiro do cliente. A separacao
    em emit() (levanta) + main() (traduz para die) mantem o comportamento do
    script e abre a porta para o cliente, sem duas copias do protocolo.
    """


def die(msg):
    print(f"push_mod_emit: {msg}", file=sys.stderr)
    sys.exit(1)


def emit(sock_addr, path, name, size, timeout=SOCKET_TIMEOUT):
    """Envia <path> como push_mod <name> <size>. Devolve a resposta do companion.

    Esta e a FONTE UNICA do envio. Quem chamar nao deve montar cabecalho nem
    payload: o ponto de ter uma funcao e nao haver um segundo lugar onde o
    protocolo possa divergir do companion.
    """
    try:
        expected = int(size)
    except (TypeError, ValueError):
        raise EmitError(f"tamanho inválido: {size!r}")
    if expected <= 0:
        raise EmitError(f"tamanho inválido: {expected}")
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise EmitError(f"não abri {path}: {exc}")
    if len(data) != expected:
        raise EmitError(
            f"tamanho divergiu do anunciado: arquivo tem {len(data)}, "
            f"esperado {expected}")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        if sock_addr.startswith("@"):
            sock.connect("\0" + sock_addr[1:])
        else:
            sock.connect(sock_addr)
        sock.sendall(f"push_mod {name} {expected}\n".encode("ascii"))
        # sendall, nao send: ele reenvia o que o kernel aceitou pela metade.
        # O cliente antigo cortava o payload em pedacos de 8 KiB para "nao
        # passar do limite de um send()", mas e exatamente o que o sendall ja
        # faz — o pedacinho era redundancia, nao protecao.
        sock.sendall(data)
        # CONTRATO DO TRANSPORTE (companion-followups #1): depois do payload o
        # emissor FECHA o lado de escrita. O EOF é a prova de que não sobrou
        # byte — sem isso o companion recusa com "emissor nao fechou o lado de
        # escrita" depois do SO_RCVTIMEO dele, em vez de aceitar calado um
        # buffer com resto envenenando o próximo comando. recv segue
        # funcionando: o shutdown só fecha a direção de escrita.
        sock.shutdown(socket.SHUT_WR)
        reply = sock.recv(256)
    except (OSError, UnicodeEncodeError) as exc:
        raise EmitError(f"falha no socket: {exc}")
    finally:
        sock.close()
    try:
        return reply.decode().strip()
    except UnicodeDecodeError:
        raise EmitError("resposta do companion não é texto")


def main(argv):
    if len(argv) != 5:
        die("uso: push_mod_emit.py <socket> <arquivo> <nome> <size>")
    _, sock_addr, path, name, size_arg = argv
    try:
        print(emit(sock_addr, path, name, size_arg))
    except EmitError as exc:
        die(str(exc))


if __name__ == "__main__":
    main(sys.argv)
