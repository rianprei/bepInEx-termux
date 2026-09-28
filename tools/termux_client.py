#!/usr/bin/env python3
# Termux Client para Zygisk BC POC Companion Process
#
# Conecta ao socket abstract @bc_companion exposto pelo companion process.
#
# Uso:
#   python3 termux_client.py ping
#   python3 termux_client.py status
#   python3 termux_client.py stream      # fica aberto, imprime eventos em tempo real
#   python3 termux_client.py list_mods
#   python3 termux_client.py toggle_mod appUpdateDraw
#
# BEPINEX_COMPANION_SOCKET sobrescreve o socket (usado pelo teste de host).

import socket
import sys

# O socket do companion. O default e o de producao; o override por ambiente
# existe para o TESTE de host, que sobe um abstract socket proprio porque o
# abstract do companion (SOCKET_NAME do companion) so existe no aparelho.
#
# Sem isso, o teste tinha que COPIAR este arquivo e trocar o socket — e um teste
# que roda uma copia nao prova nada sobre o arquivo: um dia o cliente ganha uma
# mudanca e a copia fica desatualizada, e o teste continua verde. (Achado do
# hermes em 7d084a5.)
#
# O override e o nome do socket ABSTRACT SEM o byte NUL inicial, porque o valor
# de uma variavel de ambiente e uma string C terminada em NUL e nao comporta um
# NUL no comeco. O cliente recoloca o NUL, entao o override continua sendo um
# abstract socket — o mesmo mecanismo de producao, nao um filesystem socket
# THROUGH (que exigiria outras garantias de permissao).
#
# O default continua sendo o do companion, entao em producao nada muda.
import os

_override = os.environ.get('BEPINEX_COMPANION_SOCKET')
SOCKET_NAME = ('\0' + _override) if _override else '\0bc_companion'

# Envio de arquivo: o companion le <tamanho> bytes, entao o cliente precisa de
# tempo suficiente para um arquivo grande e nao pode segurar a conexao aberta
# para sempre. PUSH_TIMEOUT e menor que o timeout de comando: um push que
# travou e pior do que um comando que travou, porque nao tem resposta util.
PUSH_TIMEOUT = 20.0

# ---------------------------------------------------------------------------
# O EMISSOR E A FONTE UNICA DO ENVIO (achado do hermes)
# ---------------------------------------------------------------------------
# Este cliente tinha uma copia propria do protocolo push_mod: montava o
# cabecalho, lia o arquivo e mandava o payload em pedacos. O repositorio ja
# tinha tools/push_mod_emit.py, que e o emissor oficial e o mesmo que
# mods/*/deploy.sh usa — duas copias do mesmo protocolo, e o jeito delas
# divergirem nao era hipotetico, ja tinha sido (uma mandava so o cabecalho).
# Agora o cliente importa o emissor e nao tem mais nenhuma logica de envio.
#
# O import e aqui no topo, e nao dentro de push_mod(), por escolha: o emissor
# e ARTEFATO DO MESMO INSTALL. Se ele faltar no destino, o instalador esta
# quebrado, e um cliente que so funciona para 11 de 12 verbos e pior do que um
# cliente que recusa dizendo o nome do arquivo que falta. Erro claro logo no
# primeiro uso, em vez de um NameError cru mais adiante.
_here = os.path.dirname(os.path.abspath(__file__))
if _here not in sys.path:
    sys.path.insert(0, _here)

try:
    import push_mod_emit
except ImportError as _exc:  # pragma: no cover - so quando o install esta torto
    print(
        "error: push_mod_emit.py nao esta no mesmo diretorio do cliente (%s).\n"
        "       O cliente nao tem mais uma copia da logica de envio: ele usa o "
        "emissor oficial.\n"
        "       Reinstale com tools/install_termux_client.sh, que instala os dois.\n"
        "       (detalhe do import: %s)" % (_here, _exc),
        file=sys.stderr,
    )
    sys.exit(2)

# Cores ANSI por nível de log — fonte real: BepInEx/BepInEx,
# Console/Unix/TtyHandler.cs L101-104 (ansiColorMapping[], indexado por
# ConsoleColor) + L163-164 (template \e[{x>7 ? 82+x : 30+x}m), LogEventArgs.cs
# pro formato de linha. Contra-intuitivo, medido não chutado: Fatal usa
# vermelho BRILHANTE (91), Error usa vermelho ESCURO (31) — ordem invertida
# do que se espera; Warning é amarelo brilhante (93), não o 33 padrão;
# Info e Debug são a MESMA cor (90, cinza).
_LEVEL_COLOR = {
    "Fatal": "\033[91m",
    "Error": "\033[31m",
    "Warning": "\033[93m",
    "Info": "\033[90m",
    "Debug": "\033[90m",
    "Message": "\033[97m",  # branco brilhante — LogLevel.cs:87 → White(15) → TtyHandler.cs:104/164
}
_RESET = "\033[0m"


def _colorize_line(line: str) -> str:
    """Aplica cor ANSI na linha se reconhecer '[Nível ...]' no início.
    Linha sem esse padrão (erro de conexão, resposta pontual) passa reta."""
    if not line.startswith("["):
        return line
    # formato: "[HH:MM:SS] [Nível  :Fonte] resto" — acha o nível no 2º []
    try:
        second = line.index("[", line.index("]") + 1)
        end = line.index(":", second)
        level = line[second + 1:end].strip()
    except ValueError:
        return line
    color = _LEVEL_COLOR.get(level)
    if not color:
        return line
    return f"{color}{line}{_RESET}"

def send_command(cmd):
    """Envia um comando pontual e imprime a resposta única.
    Achado real (reproduzido ao vivo 2026-09-14): 1 único recv(4096) cortava
    respostas multi-linha (list_mods manda 4 write_all() separados) — o
    kernel não garante coalescer tudo antes do 1º recv() chegar. Servidor
    fecha a conexão depois de responder (handle_termux_request retorna
    false pro caminho não-stream), então loop até EOF é o jeito certo,
    não frágil como confiar num recv() só cobrir tudo."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect(SOCKET_NAME)
        sock.sendall((cmd + '\n').encode())
        chunks = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
        resp = b''.join(chunks).decode()
        if not resp:
            return "error: resposta vazia (companion rejeitou por SO_PEERCRED? UID não autorizado)"
        return resp
    except ConnectionRefusedError:
        return "error: connection refused (companion not running?)"
    except FileNotFoundError:
        return "error: socket not found (companion not running?)"
    except socket.timeout:
        return "error: timeout ao conectar/ler — companion não responde"
    except (ConnectionResetError, BrokenPipeError):
        return "error: conexão resetada (companion morreu ou rejeitou)"
    except OSError as e:
        return f"error: falha de conexão ({e})"
    finally:
        sock.close()

def push_mod(path):
    """Envia um arquivo .so para o companion instalar.

    Nao ha mais logica de envio aqui: o protocolo e do push_mod_emit (fonte
    unica, a mesma que mods/*/deploy.sh usa). Esta funcao so traduz o erro
    do emissor para o formato "error: ..." que o resto do cliente devolve.

    ACHADO DO HERMES (bug real, agora coberto pelo emissor): a versao
    anterior desta funcao mandava SO o cabecalho "push_mod <nome> <tamanho>"
    e nenhum payload. O companion (jni/companion.cpp, handle_push_mod) le
    EXATAMENTE <tamanho> bytes depois do cabecalho, entao ficava esperando
    payload que nunca chegava: timeout, e o arquivo nunca aparecia em
    BC_MODS_DIR.
    """
    # O emissor trabalha com '@nome' para o namespace abstrato; o cliente
    # guarda o socket com o byte NUL, que e a mesma coisa. Esta conversao e a
    # unica adaptacao entre os dois, e ela mora AQUI porque quem conhece o
    # formato do cliente e o cliente.
    addr = "@" + SOCKET_NAME[1:] if SOCKET_NAME.startswith("\0") else SOCKET_NAME
    name = os.path.basename(path)
    try:
        # O size declarado e o que foi lido do arquivo, nunca o do stat: se o
        # arquivo encolher entre o stat e o envio, declarar o stat seria
        # mandar um cabecalho que o companion nunca satisfaz. O emissor le o
        # arquivo e REConfere o tamanho contra o que saiu, entao o que
        # passamos aqui e a checagem, nao a fonte da verdade.
        with open(path, "rb") as f:
            payload = f.read()
    except OSError as e:
        return "error: falha ao ler %s (%s)" % (path, e)
    if not payload:
        # size <= 0 e recusado pelo companion com "invalid size"; melhor dizer
        # isso aqui do que mandar cabecalho com 0 e esperar timeout.
        return "error: arquivo vazio: %s" % path
    try:
        resp = push_mod_emit.emit(addr, path, name, len(payload),
                                  timeout=PUSH_TIMEOUT)
    except push_mod_emit.EmitError as e:
        return "error: %s" % e
    if not resp:
        return "error: resposta vazia ao enviar %s" % name
    return resp


def stream():
    """Abre conexão keep-alive e imprime cada linha de evento de hook
    (tail -f do LogOutput.log do BepInEx) até o usuário interromper
    (Ctrl-C) ou o companion morrer."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(SOCKET_NAME)
    except Exception as e:
        print(f"error connecting: {e}")
        return
    sock.sendall(b'stream\n')
    print("[stream] conectado. events ao vivo: (Ctrl-C pra sair)")
    # Buffer de linha: recv() não garante alinhamento com '\n' — sem isso,
    # a cor por nível (que olha o início da linha) coloriria fragmento
    # errado quando uma linha chega partida em dois recv().
    buf = ""
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                print("[stream] companion fechou a conexão — saindo.")
                break
            buf += data.decode(errors='replace')
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                sys.stdout.write(_colorize_line(line) + "\n")
            sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n[stream] interrompido.")
    finally:
        sock.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 termux_client.py <command> [arg]")
        print("Commands: ping | status | stream | list_mods | toggle_mod <name> | set_mod <name> <valor> | push_mod <arquivo.so> | unpatch_mod <name> | repatch_mod <name> | list_patches | hook_overhead | reload_config")
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == "stream":
        stream()
        return
    if cmd == "push_mod":
        if len(sys.argv) < 3:
            print("Usage: push_mod <caminho/do/mod.so>")
            sys.exit(1)
        print(push_mod(sys.argv[2]))
        return
    if cmd == "toggle_mod":
        if len(sys.argv) < 3:
            print("Usage: toggle_mod <nome>  (appInit|appUpdateDraw|appTouch|appKey)")
            sys.exit(1)
        full = "toggle_mod " + sys.argv[2]
    else:
        full = ' '.join(sys.argv[1:])
    print(send_command(full))

if __name__ == '__main__':
    main()