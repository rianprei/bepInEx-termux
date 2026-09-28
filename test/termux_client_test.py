#!/usr/bin/env python3
"""test/termux_client_test.py — o cliente REAL contra o despacho do Termux.

ACHADO A4 DO WIRING-AUDIT (139aceb): 11 dos 12 verbos do companion tem como
UNICO sender o REPL humano do Termux, e a cadeia quebrava fora da maquina do
autor porque o console apontava para um arquivo que NINGUEM instalava.

Este teste fecha o lado do cliente. Ele fala o protocolo textual de verdade
(termux_client.py, o arquivo do repo, SEM MODIFICACAO) contra um despachante que
responde o que o companion responde, verbo por verbo.

O socket e ABSTRACT ('\\0bc_companion'), que e o que o companion usa, e o Python
faz abstract socket so com um byte NUL no inicio do nome — entao o teste sobe um
abstract de verdade e o cliente conecta nele sem nenhuma adaptacao. Um socketpair
nao serviria: o cliente abriria o proprio socket e nao veria o par.

Os verbos e as respostas sao os de jni/companion.cpp, nao inventados aqui.

O que ele pega:
  - verbo que o cliente monta mas o companion nao tem (unknown_command nos dois
    sentidos);
  - resposta multi-linha cortada — list_mods faz 4 write_all separados, e foi o
    bug real de 2026-09-14 em que um recv unico cortava a resposta;
  - o arquivo AUSENTE no staging (a sabotagem): sem ele nao ha cliente para
    falar, e o teste DIZ isso em vez de pular em silencio.
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CLIENT_SRC = os.path.join(REPO, "tools", "termux_client.py")
INSTALLER = os.path.join(REPO, "tools", "install_termux_client.sh")
CONSOLE = os.path.join(REPO, "termux-console", "bepin-console")

# Abstract socket so existe no namespace do Linux; o prefixo NUL e o que diz
# "abstract". O companion usa o mesmo truque (SOCKET_NAME = '\\0bc_companion').
SOCK_NAME = "\0bepin_termux_client_test"

fails = []


def check(name, ok):
    print("  [%s] %s" % ("PASS" if ok else "FAIL", name))
    if not ok:
        fails.append(name)


# ---------------------------------------------------------------------------
# O despacho. Fonte: jni/companion.cpp. As strings sao as de la.
# ---------------------------------------------------------------------------
PONTUAL = {
    "ping": "pong",
    "status": "companion_active",
    "reload_config": "ok: reload signal sent",
}
PONTUAL_COM_ARG = {
    "unpatch_mod appInit": "ok: unpatch signal sent",
    "repatch_mod appInit": "ok: repatch signal sent",
    "toggle_mod appInit": "ok: appInit=on",
    "set_mod appInit off": "ok: appInit=off",
}
# list_mods: 4 write_all separados (handle_list_mods) -- o multi-linha que um
# recv unico nao pega.
LIST_MODS = ["appInit=on", "appUpdateDraw=on", "appTouch=off", "appKey=off"]
LIST_PATCHES = "patches 0"
HOOK_OVERHEAD_PREFIX = "avg_dispatcher_overhead_us="


def despachante(sock):
    """Um pedido por conexao, como o companion (que fecha apos responder)."""
    buf = b""
    while b"\n" not in buf:
        r = sock.recv(4096)
        if not r:
            return
        buf += r
    linha = buf.split(b"\n", 1)[0].decode(errors="replace").strip()

    if linha == "stream":
        # keep-alive: o companion NAO responde e NAO fecha
        sock.sendall(b"[stream] conectado. events ao vivo: (Ctrl-C pra sair)\n")
        while sock.recv(4096):
            pass
        return
    if linha in PONTUAL:
        sock.sendall((PONTUAL[linha] + "\n").encode())
    elif linha in PONTUAL_COM_ARG:
        sock.sendall((PONTUAL_COM_ARG[linha] + "\n").encode())
    elif linha == "list_mods":
        for l in LIST_MODS:
            sock.sendall((l + "\n").encode())
    elif linha == "list_patches":
        sock.sendall((LIST_PATCHES + "\n").encode())
    elif linha == "hook_overhead":
        sock.sendall((HOOK_OVERHEAD_PREFIX + "7\n").encode())
    else:
        sock.sendall(b"unknown_command\n")
    sock.close()


def subir_despachante():
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        srv.bind(SOCK_NAME)
    except OSError as e:
        # abstract ja em uso (teste anterior morreu sem limpar)
        return None, e
    srv.listen(8)

    def loop():
        while True:
            try:
                c, _ = srv.accept()
            except OSError:
                return
            threading.Thread(target=despachante, args=(c,), daemon=True).start()

    threading.Thread(target=loop, daemon=True).start()
    return srv, None


def rodar_cliente(cli_path, verbos, timeout=20):
    """Roda o cliente REAL, e devolve a saida dele."""
    cli = subprocess.Popen([sys.executable, cli_path] + list(verbos),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True, stdin=subprocess.DEVNULL)
    try:
        out, err = cli.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        cli.kill()
        out, err = cli.communicate()
        return out, err, -9
    return out, err, cli.returncode


def main():
    print("== termux_client: o cliente REAL contra o despacho do Termux ==")

    # --- 0. o arquivo versionado e o staging ------------------------------
    if not os.path.isfile(CLIENT_SRC):
        check("o cliente esta versionado em tools/termux_client.py", False)
        print("termux_client_test: sem cliente para testar", file=sys.stderr)
        return 1
    check("o cliente esta versionado em tools/termux_client.py", True)

    root = tempfile.mkdtemp(prefix="bepin-termux-home-")
    dest_rel = "battlecats-mods/zygisk-bc-poc/termux_client.py"
    r = subprocess.run(["bash", INSTALLER, root], capture_output=True, text=True)
    check("o instalador roda", r.returncode == 0)
    dest = os.path.join(root, dest_rel)
    if not os.path.isfile(dest):
        # SABOTAGEM: o staging nao colocou o arquivo. Dizer e falhar, nao pular.
        check("o instalador p\u00f5e o cliente no caminho que o console procura", False)
        print("  (SAHBA: o staging nao colocou o cliente -- %s)" % dest, file=sys.stderr)
        return 1
    check("o instalador p\u00f5e o cliente no caminho que o console procura", True)
    check("o cliente instalado e byte a byte o do repo",
          open(dest, "rb").read() == open(CLIENT_SRC, "rb").read())

    # e o console aponta para ESSE caminho
    expect_rel = None
    for line in open(CONSOLE, encoding="utf-8").read().splitlines():
        if line.strip().startswith("CLIENT="):
            v = line.split("=", 1)[1].strip()
            expect_rel = v[2:] if v.startswith("~/") else v.lstrip("/")
            break
    check("o console aponta para o MESMO caminho que o staging instala (%s)" % expect_rel,
          expect_rel == dest_rel)

    srv, err = subir_despachante()
    if srv is None:
        check("o despacho subiu (abstract socket)", False)
        print("  nao subi o abstract socket: %s" % err, file=sys.stderr)
        return 1
    check("o despacho subiu num abstract socket (como o companion)", True)

    # --- 1. cada verbo tem request e resposta ----------------------------
    # O cliente tem que falar com o socket abstract; o tools/termux_client.py
    # tem o nome fixo do companion, entao o teste aponta o cliente para o
    # abstract de teste por uma copia minima do arquivo (mesmo codigo, so o
    # SOCKET_NAME trocado) — OU, se o cliente aceitar override, usa ele.
    with open(dest, encoding="utf-8") as f:
        src = f.read()
    if "SOCKET_NAME = '\\0bc_companion'" in src:
        patched = src.replace("SOCKET_NAME = '\\0bc_companion'",
                              "SOCKET_NAME = %r" % SOCK_NAME)
        if patched == src:
            print("  nao achei a linha SOCKET_NAME para apontar o cliente pro "
                  "socket de teste -- o cliente mudou de forma", file=sys.stderr)
            return 1
        cli_path = os.path.join(root, "termux_client_test_socket.py")
        with open(cli_path, "w", encoding="utf-8") as f:
            f.write(patched)
        check("o cliente e o do repo, so com o SOCKET_NAME do teste", True)
    else:
        cli_path = dest

    for verbo, esperado in sorted(PONTUAL.items()):
        out, err_txt, rc = rodar_cliente(cli_path, [verbo])
        check("%s -> %r" % (verbo, esperado), esperado in out)

    for verbo, esperado in sorted(PONTUAL_COM_ARG.items()):
        out, err_txt, rc = rodar_cliente(cli_path, verbo.split())
        check("%s -> %r" % (verbo, esperado), esperado in out)

    # --- 2. multi-linha: o bug de 2026-09-14 --------------------------
    out, err_txt, rc = rodar_cliente(cli_path, ["list_mods"])
    faltando = [l for l in LIST_MODS if l not in out]
    check("list_mods devolve as %d linhas (recv unico cortaria)" % len(LIST_MODS),
          not faltando)
    if faltando:
        print("    faltaram: %s" % faltando, file=sys.stderr)

    out, err_txt, rc = rodar_cliente(cli_path, ["list_patches"])
    check("list_patches -> %r" % LIST_PATCHES, LIST_PATCHES in out)

    out, err_txt, rc = rodar_cliente(cli_path, ["hook_overhead"])
    check("hook_overhead -> prefixo avg_dispatcher_overhead_us=",
          HOOK_OVERHEAD_PREFIX in out)

    # --- 3. verbo que o companion nao tem -------------------------------
    out, err_txt, rc = rodar_cliente(cli_path, ["verbo_que_nao_existe"])
    check("verbo desconhecido -> unknown_command", "unknown_command" in out)

    # --- 4. o verbo stream, que e o que o console roda em background -----
    cli = subprocess.Popen([sys.executable, cli_path, "stream"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True, stdin=subprocess.DEVNULL)
    time.sleep(1.5)
    vivo = cli.poll() is None
    cli.kill()
    try:
        cli.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        pass
    check("stream fica conectado (keep-alive, o console roda em background)", vivo)

    srv.close()
    print("== Resultado: %s (%d falhas) ==" %
          ("TODOS PASSARAM" if not fails else "HOUVE FALHAS", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
