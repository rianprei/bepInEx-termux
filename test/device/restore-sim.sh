#!/bin/sh
# Simulação LOCAL do device_test.sh (sem device, sem adb de verdade) para provar
# as três garantias que não dá pra ver no dry-run:
#   (a) run interrompido (marcador no device, ou backup sem a pasta de mods) é
#       RECUPERADO antes de mexer, e o backup nunca é apagado no início;
#   (3) snapshot vazio = FALHA explícita, nunca "device restaurado";
#   (5) interrupção durante a espera: o restore roda UMA vez, a saída é != 0 e
#       o device volta ao estado inicial. O caminho de interrupção é exercitado
#       pelo gancho T1_SIM_EXIT do kit (exit sem cleanup) em vez de sinal: a
#       entrega de SIGINT para um sh esperando em filho não é reproduzível
#       neste harness não-interativo — no terminal, Ctrl-C entrega no grupo de
#       processos e cai no MESMO trap (ver tools/device_test.sh: trap INT/TERM
#       chama restore e sai 130).
#
# Como: adb/su falsos no PATH. O "device" é um diretório temporário e o su
# falso reescreve /data/... para $DEV. O resto do kit (backup .part+rename,
# marcador, snapshot, traps) roda de verdade.
set -eu
KIT=$(cd "$(dirname "$0")/../.." && pwd)/tools/device_test.sh
SA2=$(cd "$(dirname "$0")/sa2" && pwd)
SA2_FIELD=$(cd "$(dirname "$0")/sa2-field" && pwd)
PKG=com.fake.game
FAILED=0
ok() { echo "  ok: $1"; }
bad() { echo "  FALHOU: $1"; FAILED=1; }
check() { if [ "$1" = 0 ]; then ok "$2"; else bad "$2"; fi; }

ROOT=$(mktemp -d)
# KEEP_SIM_ROOT=1 mantém o device temporário para depurar (imprime o path)
cleanup() { [ -n "${KEEP_SIM_ROOT:-}" ] || rm -rf "$ROOT"; }
trap cleanup EXIT
BINDIR="$ROOT/bin"
mkdir -p "$BINDIR"

# device limpo por cenário: estado inicial = 1 mod + 1 lib do usuário + log.txt
new_device() {
    export DEV="$ROOT/dev-$$-$1"
    rm -rf "$DEV"
    mkdir -p "$DEV/data/local/tmp/mods/$PKG" "$DEV/data/data/$PKG/files/bepinex"
    printf 'mod do usuario\n' > "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so"
    printf '11:00:00 [loader] mod carregado\n' > "$DEV/data/data/$PKG/files/bepinex/log.txt"
    printf 'lib do usuario\n' > "$DEV/data/data/$PKG/files/bepinex/libu_noads.so"
    touch "$DEV/data/local/tmp/mods/$PKG/u_patch.so"
    export FAKE_LOGCAT="$ROOT/logcat-$$.txt"; : > "$FAKE_LOGCAT"
    export FAKE_PID=4242
}
new_device a

# --- adb/su falsos -------------------------------------------------------
# su: lê o comando do stdin e roda com /data reescrito para $DEV/data.
cat > "$BINDIR/su" <<'EOF'
#!/bin/sh
# ${DEV} é do ambiente do SIM, lido agora: cada cenário tem seu device.
sed "s#/data/#${DEV:?}/data/#g" | sh
EOF
# chown/chmod falsos: no device o su é root; aqui não, e o kit tolera a falha
# mas o push do staging (que exige o dir criado) precisa passar.
printf '#!/bin/sh\nexit 0\n' > "$BINDIR/chown"
printf '#!/bin/sh\nexit 0\n' > "$BINDIR/chmod"
# adb: shell su (stdin) / push / get-state / logcat / am / monkey / pidof.
cat > "$BINDIR/adb" <<'EOF'
#!/bin/sh
case "$1" in
  get-state) echo device ;;
  logcat) [ "$2" = "-c" ] && exit 0; cat "$FAKE_LOGCAT" 2>/dev/null ;;
  push) cp "$2" "${3/\/data\//$DEV/data/}" ;;
  shell)
      shift; case "$1" in
        su) su ;;
        am) shift 2 >/dev/null; : ;;
        monkey) sleep "${FAKE_SLEEP:-0}" ;;
        pidof) echo "$FAKE_PID" ;;
        *) : ;;
      esac ;;
  *) : ;;
esac
EOF
chmod 755 "$BINDIR/su" "$BINDIR/adb" "$BINDIR/chown" "$BINDIR/chmod"
export PATH="$BINDIR:$PATH"

echo "== (a) run anterior interrompido: backup sem a pasta de mods =="
# simula o pior caso: o run morreu ENTRE o rm -rf e o mv, então os mods do
# usuário existem só no backup.
rm -rf "$DEV/data/local/tmp/mods/$PKG"
mkdir -p "$DEV/data/local/tmp/t1-bak-$PKG"
printf 'mod do usuario (so no backup)\n' > "$DEV/data/local/tmp/t1-bak-$PKG/sa2ammo.so"
touch "$DEV/data/local/tmp/t1-bak-$PKG/u_patch.so"
printf 'before_sha=deadbeef\n' > "$DEV/data/local/tmp/t1-inprogress-$PKG"
printf '10:00:00 [u_patch] log antigo do run morto\n' > "$DEV/data/data/$PKG/files/bepinex/log.txt"
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1 || true)
echo "$OUT" | grep -q "run anterior interrompido detectado" && ok "aviso de run interrompido" || bad "aviso de run interrompido"
[ -f "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" ] && ok "mods voltaram do backup" || bad "mods voltaram do backup"
grep -q "so no backup" "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" 2>/dev/null && ok "conteudo do backup preservado" || bad "conteudo do backup preservado"
[ ! -f "$DEV/data/local/tmp/t1-inprogress-$PKG" ] && ok "marcador removido" || bad "marcador removido"
# o run novo tambem tem que ter restaurado o estado final
if echo "$OUT" | grep -q "device restaurado"; then ok "estado final = inicial"; else bad "estado final = inicial" ; echo "$OUT" | tail -5; fi

echo "== (3) snapshot vazio = falha, nunca 'device restaurado' =="
new_device c
cat > "$BINDIR/adb" <<'EOF'
#!/bin/sh
case "$1" in
  get-state) echo device ;;
  logcat) [ "$2" = "-c" ] && exit 0; cat "$FAKE_LOGCAT" 2>/dev/null ;;
  push) cp "$2" "${3/\/data\//$DEV/data/}" ;;
  shell) shift; case "$1" in
      su) : ;;   # su falso: nada responde -> snapshot vazio
      am) shift 2 >/dev/null; : ;;
      monkey) : ;;
      pidof) echo "$FAKE_PID" ;;
      *) : ;;
    esac ;;
  *) : ;;
esac
EOF
chmod 755 "$BINDIR/adb"
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1 || true)
echo "$OUT" | grep -q "snapshot inicial vazio" && ok "snapshot vazio = FALHA" || bad "snapshot vazio = FALHA"
echo "$OUT" | grep -q "device restaurado" && bad "nao pode dizer 'restaurado' com snapshot vazio" || ok "nao disse 'restaurado' com snapshot vazio"
cat > "$BINDIR/adb" <<'EOF'
#!/bin/sh
case "$1" in
  get-state) echo device ;;
  logcat) [ "$2" = "-c" ] && exit 0; cat "$FAKE_LOGCAT" 2>/dev/null ;;
  push) cp "$2" "${3/\/data\//$DEV/data/}" ;;
  shell) shift; case "$1" in
      su) su ;;
      am) shift 2 >/dev/null; : ;;
      monkey) sleep "${FAKE_SLEEP:-0}" ;;
      pidof) echo "$FAKE_PID" ;;
      *) : ;;
    esac ;;
  *) : ;;
esac
EOF
chmod 755 "$BINDIR/adb"

echo "== (5) interrupção: restaura uma vez, saída != 0, device volta ao inicial =="
new_device e
# arquivo do USUÁRIO que já estava no estado inicial: tem que continuar lá
# depois do restore (o restore volta ao inicial, não a um mods vazio)
printf '12:00:00 [u_patch] patch do usuario\n' > "$DEV/data/local/tmp/mods/$PKG/meu_mod.patch"
T1_SIM_EXIT=espera sh "$KIT" "$PKG" "$SA2" 60 > "$ROOT/out.txt" 2>&1 && RC=0 || RC=$?
[ "$RC" != 0 ] && ok "saida != 0 na interrupcao (foi $RC)" || bad "saida != 0 na interrupcao (veio $RC)"
n=$(grep -c "restaurando device" "$ROOT/out.txt" || true)
[ "$n" = 1 ] && ok "restore rodou uma vez só" || bad "restore rodou uma vez só (rodou $n)"
[ -f "$DEV/data/local/tmp/mods/$PKG/meu_mod.patch" ] && ok "mod do usuário preservado" || bad "mod do usuário preservado"
[ -f "$DEV/data/local/tmp/mods/$PKG/t1_return.patch" ] && bad "artefato do teste sobrou" || ok "artefato do teste removido"
[ -d "$DEV/data/data/$PKG/files/bepinex" ] && ok "files/bepinex existe no final" || bad "files/bepinex existe no final"
grep -q "log inicial\|11:00:00 \[loader\]" "$DEV/data/data/$PKG/files/bepinex/log.txt" 2>/dev/null \
    && ok "log.txt voltou ao inicial" || bad "log.txt voltou ao inicial"
[ ! -f "$DEV/data/local/tmp/t1-inprogress-$PKG" ] && ok "marcador limpo" || bad "marcador limpo"

echo "== (5b) interrupcao logo apos instalar (jogo nem subiu) =="
new_device e2
T1_SIM_EXIT=instalado sh "$KIT" "$PKG" "$SA2" 60 > "$ROOT/out2.txt" 2>&1 && RC=0 || RC=$?
[ "$RC" != 0 ] && ok "saida != 0 (foi $RC)" || bad "saida != 0 (veio $RC)"
n=$(grep -c "restaurando device" "$ROOT/out2.txt" || true)
[ "$n" = 1 ] && ok "restore rodou uma vez só" || bad "restore rodou uma vez só (rodou $n)"
[ -f "$DEV/data/local/tmp/mods/$PKG/t1_static.patch" ] && bad "patch instalado sobrou" || ok "patch instalado removido"
[ -f "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" ] && ok "mod do usuário preservado" || bad "mod do usuário preservado"

echo "== (1) sem backup verificado de files/bepinex: a pasta do usuario NAO e apagada =="
# O risco do achado: o restore fazia 'rm -rf files/bepinex' e so depois tentava
# repor do BAK_OUT — se o backup nao existisse (ou nao conferisse), o
# dump.tsv/crashguard do usuario sumia e o run ainda dizia "restaurado".
# Aqui o adb falso apaga o BAK_OUT no launch, como se o backup nunca tivesse
# sido valido.
new_device g
printf 'dump do usuario\n' > "$DEV/data/data/$PKG/files/bepinex/dump.tsv"
cat > "$BINDIR/adb" <<'EOF'
#!/bin/sh
case "$1" in
  get-state) echo device ;;
  logcat) [ "$2" = "-c" ] && exit 0; cat "$FAKE_LOGCAT" 2>/dev/null ;;
  push) cp "$2" "${3/\/data\//$DEV/data/}" ;;
  shell) shift; case "$1" in
      su) su ;;
      am) shift 2 >/dev/null; : ;;
      monkey) rm -rf "$DEV/data/local/tmp/t1-bak-$FAKE_PKG" "$DEV/data/local/tmp/t1-bak-out-$FAKE_PKG" ;;
      pidof) echo "$FAKE_PID" ;;
      *) : ;;
    esac ;;
  *) : ;;
esac
EOF
chmod 755 "$BINDIR/adb"
export FAKE_PKG="$PKG"
sh "$KIT" "$PKG" "$SA2" 6 > "$ROOT/out3.txt" 2>&1 && RC=0 || RC=$?
unset FAKE_PKG
grep -q "NADA APAGADO" "$ROOT/out3.txt" && ok "disse que nao apagou" || bad "disse que nao apagou"
grep -q "device restaurado" "$ROOT/out3.txt" && bad "nao pode dizer 'restaurado'" || ok "nao disse 'restaurado'"
[ -f "$DEV/data/data/$PKG/files/bepinex/dump.tsv" ] && ok "dump.tsv do usuario intacto" || bad "dump.tsv do usuario intacto"
[ -f "$DEV/data/local/tmp/t1-inprogress-$PKG" ] && ok "marcador de pé para o proximo run" || bad "marcador de pé para o proximo run"
[ "$RC" != 0 ] && ok "saida != 0 (foi $RC)" || bad "saida != 0 (veio $RC)"
# o adb normal volta
cat > "$BINDIR/adb" <<'EOF'
#!/bin/sh
case "$1" in
  get-state) echo device ;;
  logcat) [ "$2" = "-c" ] && exit 0; cat "$FAKE_LOGCAT" 2>/dev/null ;;
  push) cp "$2" "${3/\/data\//$DEV/data/}" ;;
  shell) shift; case "$1" in
      su) su ;;
      am) shift 2 >/dev/null; : ;;
      monkey) sleep "${FAKE_SLEEP:-0}" ;;
      pidof) echo "$FAKE_PID" ;;
      *) : ;;
    esac ;;
  *) : ;;
esac
EOF
chmod 755 "$BINDIR/adb"

echo "== (campo) caso field roda separado =="
new_device f
OUT=$(sh "$KIT" "$PKG" "$SA2_FIELD" 6 2>&1 || true)
echo "$OUT" | grep -q "t1_field.patch" && ok "kit le o dir do caso field" || bad "kit le o dir do caso field"

if [ -n "${KEEP_SIM_ROOT:-}" ]; then echo "device temporário: $ROOT"; fi
if [ "$FAILED" = 0 ]; then
    echo "device_test-sim: OK"
    exit 0
fi
echo "device_test-sim: HOUVE FALHAS"
exit 1
