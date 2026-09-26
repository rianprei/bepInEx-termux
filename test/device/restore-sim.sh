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
# su base: lê o comando do stdin e roda com /data reescrito para $DEV/data.
# Cenários que precisam de su especial (r1/r3) sobrescrevem $BINDIR/su e
# restauram este daqui depois.
write_base_su() {
    cat > "$BINDIR/su" <<'EOF'
#!/bin/sh
# ${DEV} é do ambiente do SIM, lido agora: cada cenário tem seu device.
sed "s#/data/#${DEV:?}/data/#g" | sh
EOF
    chmod 755 "$BINDIR/su"
}
write_base_su
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
  push) _dst=$(printf '%s' "$3" | sed "s#^/data/#$DEV/data/#"); cp "$2" "$_dst" ;;
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
  push) _dst=$(printf '%s' "$3" | sed "s#^/data/#$DEV/data/#"); cp "$2" "$_dst" ;;
  shell) shift; case "$1" in
      su) _c=$(cat); case "$_c" in *test*) exit 1;; *) exit 0;; esac ;;   # su morto: triggers honestos, resto silencio -> snapshot vazio
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
  push) _dst=$(printf '%s' "$3" | sed "s#^/data/#$DEV/data/#"); cp "$2" "$_dst" ;;
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
  push) _dst=$(printf '%s' "$3" | sed "s#^/data/#$DEV/data/#"); cp "$2" "$_dst" ;;
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
  push) _dst=$(printf '%s' "$3" | sed "s#^/data/#$DEV/data/#"); cp "$2" "$_dst" ;;
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

# --- (r1) hash inicial de mods EXISTENTE falha (transiente): recusa, nada apagado
# O bug antigo: saída vazia virava "AUSENTE" e o restore apagava a pasta sem
# repor do backup. O su falo devolve vazio SÓ na 1ª leitura de hash de mods.
echo "== (r1) hash inicial de mods falha (transiente) = recusa, nada apagado =="
new_device r1
cat > "$BINDIR/su" <<'EOF'
#!/bin/sh
CMD=$(cat)
case "$CMD" in
  *"mods/$FAKE_PKG' && {"*)
    _n=$(cat "${MODS_HASH_STATE:?}" 2>/dev/null || echo 0)
    _n=$((_n + 1))
    echo "$_n" > "${MODS_HASH_STATE:?}"
    [ "$_n" -le "${MODS_HASH_FAIL_FIRST:-1}" ] && exit 0
    ;;
esac
printf '%s\n' "$CMD" | sed "s#/data/#${DEV:?}/data/#g" | sh
EOF
chmod 755 "$BINDIR/su"
export FAKE_PKG="$PKG" MODS_HASH_STATE="$ROOT/r1-state-$$"
: > "$MODS_HASH_STATE"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1) || RC=$?
echo "$OUT" | grep -q "não consegui hashear mods" && ok "recusou com hash ilegível" || bad "recusou com hash ilegível"
[ -f "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" ] && ok "mod do usuário intacto (nada apagado)" || bad "mod do usuário intacto (nada apagado)"
grep -q "mod do usuario" "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" 2>/dev/null && ok "conteúdo do mod preservado" || bad "conteúdo do mod preservado"
[ "$RC" != 0 ] && ok "saída != 0 (foi $RC)" || bad "saída != 0 (veio $RC)"
write_base_su
unset FAKE_PKG MODS_HASH_STATE

# --- (r2) mods ATUAL difere do backup órfão: conflito, nada apagado, BAK.conflict
# O bug antigo: rm -rf nos mods atuais e cp do backup por cima — mods que o
# usuário mexeu DEPOIS do crash do run anterior sumiam em silêncio.
echo "== (r2) mods atual difere do backup do run morto = conflito, nada apagado =="
new_device r2
rm -rf "$DEV/data/local/tmp/t1-bak-$PKG"
mkdir -p "$DEV/data/local/tmp/t1-bak-$PKG"
printf 'mod do usuario (so no backup)\n' > "$DEV/data/local/tmp/t1-bak-$PKG/sa2ammo.so"
touch "$DEV/data/local/tmp/t1-bak-$PKG/u_patch.so"
printf 'before_sha=deadbeef\n' > "$DEV/data/local/tmp/t1-inprogress-$PKG"
printf 'mod do usuario EDITADO depois do crash\n' > "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1) || RC=$?
echo "$OUT" | grep -q "DIFERE" && ok "detectou conflito (mod atual != backup)" || bad "detectou conflito (mod atual != backup)"
grep -q "EDITADO depois do crash" "$DEV/data/local/tmp/mods/$PKG/sa2ammo.so" 2>/dev/null \
    && ok "mod editado pelo usuário preservado" || bad "mod editado pelo usuário preservado"
[ -f "$DEV/data/local/tmp/t1-bak-$PKG.conflict/sa2ammo.so" ] && ok "cópia de segurança em .conflict" || bad "cópia de segurança em .conflict"
[ -f "$DEV/data/local/tmp/t1-bak-$PKG/sa2ammo.so" ] && ok "backup original intacto" || bad "backup original intacto"
[ "$RC" != 0 ] && ok "saída != 0 (foi $RC)" || bad "saída != 0 (veio $RC)"

# --- (r3) recuperação que não confere: aborta, backup bom preservado
# O bug antigo: recovery mangled (cp pela metade) seguia o run adiante e o
# backup NOVO (do estado quebrado) destruía o único backup bom no rm do .part.
echo "== (r3) recuperação não confere = aborta com backup bom preservado =="
new_device r3
rm -rf "$DEV/data/local/tmp/mods/$PKG"
mkdir -p "$DEV/data/local/tmp/t1-bak-$PKG"
printf 'mod do usuario (so no backup)\n' > "$DEV/data/local/tmp/t1-bak-$PKG/sa2ammo.so"
printf 'componente que o cp falho nao copia\n' > "$DEV/data/local/tmp/t1-bak-$PKG/u_patch.so"
printf 'before_sha=deadbeef\n' > "$DEV/data/local/tmp/t1-inprogress-$PKG"
cat > "$BINDIR/su" <<'EOF'
#!/bin/sh
CMD=$(cat)
case "$CMD" in
  *"cp -a /data/local/tmp/t1-bak-"*)
    # cp falho: copia só o primeiro arquivo do backup
    rm -rf "${DEV:?}/data/local/tmp/mods/$FAKE_PKG"
    mkdir -p "${DEV:?}/data/local/tmp/mods/$FAKE_PKG"
    cp "${DEV:?}/data/local/tmp/t1-bak-$FAKE_PKG/sa2ammo.so" "${DEV:?}/data/local/tmp/mods/$FAKE_PKG/" 2>/dev/null
    exit 0
    ;;
esac
printf '%s\n' "$CMD" | sed "s#/data/#${DEV:?}/data/#g" | sh
EOF
chmod 755 "$BINDIR/su"
export FAKE_PKG="$PKG"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1) || RC=$?
echo "$OUT" | grep -q "recuperação não confere" && ok "detectou recovery quebrado" || bad "detectou recovery quebrado"
[ -f "$DEV/data/local/tmp/t1-bak-$PKG/u_patch.so" ] && ok "backup bom NÃO foi destruído" || bad "backup bom NÃO foi destruído"
grep -q "componente que o cp falho nao copia" "$DEV/data/local/tmp/t1-bak-$PKG/u_patch.so" 2>/dev/null \
    && ok "conteúdo do backup bom intacto" || bad "conteúdo do backup bom intacto"
[ "$RC" != 0 ] && ok "saída != 0 (foi $RC)" || bad "saída != 0 (veio $RC)"
write_base_su
unset FAKE_PKG

# --- (r4) lock VIVO de outro run: falha limpo, lock alheio intocado
# O bug antigo: o trap de saída referenciava HASH_* não definidas (set -u
# abortava no meio com "unbound variable") — o lock alheio sobrevivia por
# acidente e a saída era ruído de shell.
echo "== (r4) lock vivo de outro run = falha limpa, lock alheio preservado =="
new_device r4
mkdir -p "$DEV/data/local/tmp/t1-lock-$PKG"
printf 'pid=99999\nhost=lock-alheio\nts=%s\n' "$(date +%s)" > "$DEV/data/local/tmp/t1-lock-$PKG/ts"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 2>&1) || RC=$?
echo "$OUT" | grep -q "VIVO" && ok "reconheceu lock vivo" || bad "reconheceu lock vivo"
echo "$OUT" | grep -Eq "unbound|parameter not set" && bad "sem crash de unbound variable" || ok "sem crash de unbound variable"
grep -q "host=lock-alheio" "$DEV/data/local/tmp/t1-lock-$PKG/ts" 2>/dev/null && ok "lock alheio intocado" || bad "lock alheio intocado"
[ "$RC" = 1 ] && ok "saída 1 (foi $RC)" || bad "saída 1 (veio $RC)"

# --- (r5) --force: órfão quebra, VIVO recusa
echo "== (r5a) --force NÃO quebra lock vivo =="
new_device r5a
mkdir -p "$DEV/data/local/tmp/t1-lock-$PKG"
printf 'pid=99999\nhost=lock-alheio\nts=%s\n' "$(date +%s)" > "$DEV/data/local/tmp/t1-lock-$PKG/ts"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 --force 2>&1) || RC=$?
echo "$OUT" | grep -q "VIVO" && ok "--force recusou lock vivo" || bad "--force recusou lock vivo"
echo "$OUT" | grep -q "lock pego" && bad "não pode pegar lock vivo com --force" || ok "não pegou lock vivo com --force"
grep -q "host=lock-alheio" "$DEV/data/local/tmp/t1-lock-$PKG/ts" 2>/dev/null && ok "lock vivo preservado" || bad "lock vivo preservado"
[ "$RC" = 1 ] && ok "saída 1 (foi $RC)" || bad "saída 1 (veio $RC)"
echo "== (r5b) --force quebra lock órfão =="
new_device r5b
mkdir -p "$DEV/data/local/tmp/t1-lock-$PKG"
printf 'pid=99999\nhost=run-morto\nts=%s\n' "$(( $(date +%s) - 1000 ))" > "$DEV/data/local/tmp/t1-lock-$PKG/ts"
RC=0
OUT=$(sh "$KIT" "$PKG" "$SA2" 6 --force 2>&1) || RC=$?
echo "$OUT" | grep -q "lock pego" && ok "--force pegou lock órfão" || bad "--force pegou lock órfão"
echo "$OUT" | grep -q "device restaurado" && ok "run do órfão completou" || bad "run do órfão completou"

if [ -n "${KEEP_SIM_ROOT:-}" ]; then echo "device temporário: $ROOT"; fi
if [ "$FAILED" = 0 ]; then
    echo "device_test-sim: OK"
    exit 0
fi
echo "device_test-sim: HOUVE FALHAS"
exit 1
