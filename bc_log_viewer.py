#!/usr/bin/env python3
# bc_log_viewer.py — Cliente Termux-style para o zygisk-bc-poc companion
#
# Conecta ao socket abstract @bc_companion via adb forward (TCP) quando usado
# de fora do device, ou direto via Unix socket quando usado dentro do device.
#
# Auth: SO_PEERCRED no socket abstract libera Termux UID + shell UID (2000)
# (pro adb forward) + root UID (0). Connection externo exige adb forward
# explícito — nenhuma porta TCP aberta no device.
#
# Uso:
#   python3 bc_log_viewer.py ping
#   python3 bc_log_viewer.py status
#   python3 bc_log_viewer.py list_mods
#   python3 bc_log_viewer.py list_patches
#   python3 bc_log_viewer.py stream
#
# Para usar via adb forward (fora do device):
#   adb forward tcp:9222 localabstract:bc_companion
#   python3 bc_log_viewer.py --host localhost --port 9222 list_patches

import socket
import sys
import argparse

# Forward declaration — resolved at runtime based on --host/--port
SOCKET_NAME = '\0bc_companion'

def create_socket(host=None, port=None, timeout=5.0):
    """Cria socket: Unix abstract (default) ou TCP (adb forward).

    Timeout aplicado ANTES do connect e mantido no socket: sem isso, um
    adb forward morto ou um companion que aceita mas não responde trava o
    cliente para sempre (recv bloqueante sem SO_RCVTIMEO).
    """
    if host is not None and port is not None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    else:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        if host is not None and port is not None:
            sock.connect((host, port))
        else:
            sock.connect(SOCKET_NAME)
    except Exception:
        sock.close()
        raise
    return sock

def send_command(cmd, host=None, port=None, timeout=5.0):
    """Envia comando sync e retorna resposta única.

    Tratamento explícito de falha de conexão:
    - Timeout (companion que aceita mas não responde / forward morto)
    - Resposta vazia (companion rejeitou por SO_PEERCRED e fechou sem
      escrever nada — mensagem clara em vez de '' silencioso)
    """
    try:
        sock = create_socket(host, port, timeout)
    except ConnectionRefusedError:
        return "error: connection refused (companion not running? verifique adb forward)\n"
    except FileNotFoundError:
        return "error: socket not found (companion not running?)\n"
    except socket.timeout:
        return "error: timeout ao conectar — adb forward pendurado ou companion não responde\n"
    except OSError as e:
        return f"error: falha de conexão ({e})\n"
    try:
        sock.sendall((cmd + '\n').encode())
        chunks = []
        while True:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                return "error: timeout esperando resposta do companion (rejeitou a conexão?)\n"
            if not data:
                break
            chunks.append(data)
        reply = b''.join(chunks).decode(errors='replace')
        if not reply:
            return "error: resposta vazia — conexão rejeitada pelo companion (SO_PEERCRED/UID?) ou protocolo errado\n"
        return reply
    except ConnectionResetError:
        return "error: conexão resetada (companion morreu ou rejeitou)\n"
    except BrokenPipeError:
        return "error: pipe quebrado (companion fechou durante envio)\n"
    except Exception as e:
        return f"error: {e}\n"
    finally:
        sock.close()

def stream(host=None, port=None, timeout=5.0):
    """Connects keep-alive, prints events em tempo real."""
    try:
        sock = create_socket(host, port, timeout)
    except ConnectionRefusedError:
        print("error: connection refused (companion not running? verifique adb forward)", file=sys.stderr)
        return 1
    except FileNotFoundError:
        print("error: socket not found (companion not running?)", file=sys.stderr)
        return 1
    except socket.timeout:
        print("error: timeout ao conectar — adb forward pendurado ou companion não responde", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"error: falha de conexão ({e})", file=sys.stderr)
        return 1
    try:
        sock.sendall(b'stream\n')
        print("[stream] conectado. Ctrl-C pra sair.")
        buf = ""
        try:
            while True:
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    # stream é keep-alive: timeout de I/O por conexão inativa
                    # seria falso positivo (companion não envia nada ocioso).
                    # Re-lê até receber dado ou EOF.
                    continue
                if not data:
                    print("[stream] companion fechou a conexão — processo do jogo morreu? reconecte quando relançar.", file=sys.stderr)
                    break
                buf += data.decode(errors='replace')
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    print(line)
        except KeyboardInterrupt:
            print("\n[stream] interrompido.")
    except BrokenPipeError:
        print("[stream] pipe quebrado (companion fechou)", file=sys.stderr)
    finally:
        sock.close()
    return 0

def main():
    parser = argparse.ArgumentParser(description='bc-poc companion client')
    parser.add_argument('command', choices=['ping', 'status', 'list_mods', 'list_patches', 'stream'])
    parser.add_argument('--host', help='TCP host (adb forward)')
    parser.add_argument('--port', type=int, help='TCP port (adb forward)')
    args = parser.parse_args()

    if args.command == 'stream':
        sys.exit(stream(args.host, args.port))
        return

    # list_patches is slow (~2.5s waiting for game snapshot)
    if args.command == 'list_patches':
        print("Aguardando snapshot do game process (~2.5s)...", file=sys.stderr)

    result = send_command(args.command, args.host, args.port)
    print(result, end='')
    if not result.endswith('\n'):
        print()

if __name__ == '__main__':
    main()
