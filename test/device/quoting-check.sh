#!/bin/sh
# Prova do mecanismo de entrega de comando pro su (host, sem device).
# Simula o adb real (junta args com espaço; o sh do device re-divide) + um
# su fake (só o 1º arg após -c vira comando com poder de root, o resto roda
# como shell comum; sem -c, lê o comando do stdin — como o su de verdade).
# O ponto NÃO é o arquivo existir, e sim COM QUE PRIVILÉGIO cada parte roda:
# no modo antigo o resto da linha cai pra shell sem root (== Permission
# denied no device real). Esperado: antigo=shell, novo=root. Saída 0 = novo ok.
set -eu
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM HUP
mkdir -p "$T/bin"
cat > "$T/bin/su" <<'EOF'
#!/bin/sh
if [ "${1:-}" = "-c" ]; then
    shift
    SU_FAKE_ROOT=1 sh -c "$1" || true
    shift
    if [ $# -gt 0 ]; then "$@" || true; fi
else
    SU_FAKE_ROOT=1 sh -s
fi
EOF
chmod +x "$T/bin/su"
cat > "$T/bin/adb" <<'EOF'
#!/bin/sh
[ "$1" = "shell" ] && shift
sh -c "$*"
EOF
chmod +x "$T/bin/adb"
cat > "$T/bin/priv" <<'EOF'
#!/bin/sh
if [ -n "${SU_FAKE_ROOT:-}" ]; then echo root; else echo shell; fi >> "$1"
EOF
chmod +x "$T/bin/priv"
export PATH="$T/bin:$PATH"

# Comando com && (como os reais do device_test.sh): o teste decide o ramo,
# e o priv registra QUEM executou.
CMD="test -d $T/nodir && priv $T/mark.txt || priv $T/mark.txt"
MARK="$T/mark.txt"

echo "--- modo antigo: adb shell su -c \"\$cmd\" ---"
old_dev() { adb shell su -c "$1"; }
rm -f "$MARK"
old_dev "$CMD" 2>/dev/null || true
if [ "$(cat "$MARK" 2>/dev/null)" = "shell" ]; then
    echo "ok: caiu pra shell sem root (== Permission denied no device real)"
else
    echo "INESPERADO: $(cat "$MARK" 2>/dev/null) (revise a simulação)"
    exit 1
fi

echo "--- modo novo: printf | adb shell su ---"
new_dev() { printf '%s\n' "$1" | adb shell su; }
rm -f "$MARK"
new_dev "$CMD"
if [ "$(cat "$MARK" 2>/dev/null)" = "root" ]; then
    echo "PASS: linha inteira (com &&) rodou como root"
else
    echo "FAIL: modo novo perdeu privilégio"; exit 1
fi

echo "--- modo novo com aspas (stat -c '%n %s', &&) ---"
CMD2="stat -c '%n %s' $T/mark.txt > $T/stat.txt && printf '%s' 'a && b' >> $T/stat.txt"
rm -f "$T/stat.txt"
new_dev "$CMD2"
if grep -q "mark.txt" "$T/stat.txt" 2>/dev/null && grep -q "a && b" "$T/stat.txt" 2>/dev/null; then
    echo "PASS: aspas simples e && intactos"
else
    echo "FAIL: quoting quebrou"; exit 1
fi
echo "quoting-check: OK"
