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


def die(msg):
    print(f"push_mod_emit: {msg}", file=sys.stderr)
    sys.exit(1)


def main(argv):
    if len(argv) != 5:
        die("uso: push_mod_emit.py <socket> <arquivo> <nome> <size>")
    _, sock_addr, path, name, size_arg = argv
    try:
        expected = int(size_arg)
    except ValueError:
        die(f"tamanho inválido: {size_arg!r}")
    if expected <= 0:
        die(f"tamanho inválido: {expected}")
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        die(f"não abri {path}: {exc}")
    if len(data) != expected:
        die(f"tamanho divergiu do anunciado: arquivo tem {len(data)}, esperado {expected}")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        if sock_addr.startswith("@"):
            sock.connect("\0" + sock_addr[1:])
        else:
            sock.connect(sock_addr)
        sock.sendall(f"push_mod {name} {expected}\n".encode("ascii"))
        sock.sendall(data)
        reply = sock.recv(256)
    except (OSError, UnicodeEncodeError) as exc:
        die(f"falha no socket: {exc}")
    finally:
        sock.close()
    try:
        print(reply.decode().strip())
    except UnicodeDecodeError:
        die("resposta do companion não é texto")


if __name__ == "__main__":
    main(sys.argv)
