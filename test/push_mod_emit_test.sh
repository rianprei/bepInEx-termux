#!/usr/bin/env bash
# test/push_mod_emit_test.sh — o emissor push_mod anuncia SIZE e envia
# exatamente SIZE bytes, nem a mais nem a menos.
#
# Sobe um companion falso (socket unix local): recebe a linha de anúncio,
# lê exatamente o tamanho anunciado e grava o que chegou. Três casos:
#   1. arquivo íntegro → anúncio == bytes recebidos == sha do arquivo;
#   2. tamanho anunciado MAIOR que o arquivo (adb push truncado) → o
#      emissor recusa ANTES de enviar, sem abrir o payload;
#   3. arquivo ausente → erro limpo.
# Sem device, sem adb: tudo em socket local com timeout curto.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=$(mktemp -d)
SERVER_PID=""
trap 'rm -rf "$WORK"; [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true' EXIT

# fixture determinística: 1024 bytes
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 4)" > "$WORK/mod.so"
SIZE=$(stat -c%s "$WORK/mod.so")
SHA=$(sha256sum "$WORK/mod.so" | cut -d' ' -f1)

# companion falso: 1 conexão, lê anúncio + payload exato, registra
cat > "$WORK/fake_companion.py" <<'PY'
import hashlib
import socket
import sys
sock_path, out_dir = sys.argv[1], sys.argv[2]
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(10)
conn, _ = srv.accept()
conn.settimeout(10)
line = b""
while not line.endswith(b"\n"):
    chunk = conn.recv(1)
    if not chunk:
        break
    line += chunk
parts = line.decode().split()
name, size = parts[1], int(parts[2])
received = b""
while len(received) < size:
    chunk = conn.recv(min(4096, size - len(received)))
    if not chunk:
        break
    received += chunk
with open(out_dir + "/got_name", "w") as fh:
    fh.write(name)
with open(out_dir + "/got_size", "w") as fh:
    fh.write(str(len(received)))
with open(out_dir + "/got_sha", "w") as fh:
    fh.write(hashlib.sha256(received).hexdigest())
conn.sendall(f"ok: {len(received)} bytes written\n".encode())
PY

run_server() {
    rm -f "$WORK/got_name" "$WORK/got_size" "$WORK/got_sha" "$WORK/sock"
    python3 "$WORK/fake_companion.py" "$WORK/sock" "$WORK" &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        [ -S "$WORK/sock" ] && break
        sleep 0.1
    done
}

stop_server() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
}

echo "push_mod_emit: (1) arquivo íntegro chega byte a byte"
run_server
python3 "$ROOT/tools/push_mod_emit.py" "$WORK/sock" "$WORK/mod.so" "02_test.so" "$SIZE" > "$WORK/out1.txt"
for _ in $(seq 1 50); do
    [ -f "$WORK/got_size" ] && break
    sleep 0.1
done
[ "$(cat "$WORK/got_name")" = "02_test.so" ] || { echo "nome divergiu" >&2; exit 1; }
[ "$(cat "$WORK/got_size")" = "$SIZE" ] || { echo "tamanho divergiu" >&2; exit 1; }
[ "$(cat "$WORK/got_sha")" = "$SHA" ] || { echo "sha divergiu" >&2; exit 1; }
grep -q "^ok:" "$WORK/out1.txt" || { echo "sem ok do companion" >&2; exit 1; }
echo "  ok: $SIZE bytes anunciados == recebidos, sha confere"
stop_server

echo "push_mod_emit: (2) anúncio maior que o arquivo recusa sem enviar"
run_server
if python3 "$ROOT/tools/push_mod_emit.py" "$WORK/sock" "$WORK/mod.so" "02_test.so" "$((SIZE + 100))" > "$WORK/out2.txt" 2>&1; then
    echo "emissor aceitou tamanho mentiroso" >&2
    exit 1
fi
grep -q "divergiu" "$WORK/out2.txt" || { echo "sem mensagem de divergência" >&2; cat "$WORK/out2.txt" >&2; exit 1; }
[ ! -f "$WORK/got_size" ] || { echo "payload foi enviado apesar da recusa" >&2; exit 1; }
echo "  ok: recusado antes de enviar"
stop_server

echo "push_mod_emit: (3) arquivo ausente é erro limpo"
if python3 "$ROOT/tools/push_mod_emit.py" "$WORK/sock" "$WORK/ausente.so" "02_test.so" "$SIZE" > "$WORK/out3.txt" 2>&1; then
    echo "emissor aceitou arquivo ausente" >&2
    exit 1
fi
echo "  ok: arquivo ausente recusado"

echo "push_mod_emit: OK"
