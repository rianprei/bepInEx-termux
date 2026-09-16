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

import socket
import sys

SOCKET_NAME = '\0bc_companion'

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
    try:
        sock.connect(SOCKET_NAME)
        sock.sendall((cmd + '\n').encode())
        chunks = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
        return b''.join(chunks).decode()
    except ConnectionRefusedError:
        return "error: connection refused (companion not running?)"
    except FileNotFoundError:
        return "error: socket not found (companion not running?)"
    except Exception as e:
        return f"error: {e}"
    finally:
        sock.close()

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
        print("Commands: ping | status | stream | list_mods | toggle_mod <name> | reload_config | hook_overhead")
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == "stream":
        stream()
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