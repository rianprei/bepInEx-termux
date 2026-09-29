#!/usr/bin/env bash
# test/tabs_offline_block_test.sh — o filtro de rede do TABS roda com iptables
# de verdade no aparelho, então aqui ele roda com um iptables de mentira.
#
# O teste existe por causa de um defeito REAL que já aconteceu: a versão
# anterior do script tinha o uid do app escrito no texto. O app foi
# reinstalado, o uid mudou de 10310 para 10361, a regra continuou casando com
# o uid velho — que não pertencia a app nenhum — e o contador de pacotes
# rejeitados ficou em zero. Quem olhasse o contador concluiria "telemetria já
# estava desligada". O filtro era teatro. Por isso o primeiro teste abaixo é
# o do uid: ele não pode existir escrito à mão em lugar nenhum do script.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/tools/tabs_offline_block.sh"
FAIL=0

ok()   { printf 'ok %s: %s\n' "$1" "$2"; }
fail() { printf 'tabs_offline_block_test: %s\n' "$1" >&2; FAIL=1; }

[ -f "$SCRIPT" ] || { fail "não achei $SCRIPT"; exit 1; }

# ── 1. sintaxe ──────────────────────────────────────────────────────────────
if sh -n "$SCRIPT" 2>/tmp/tabs-blk-syn.err; then
  ok "sintaxe" "sh -n passa"
else
  fail "sintaxe quebrada: $(cat /tmp/tabs-blk-syn.err)"
fi

# ── 2. lint ─────────────────────────────────────────────────────────────────
SHELLCHECK=$(command -v shellcheck || true)
if [ -n "$SHELLCHECK" ]; then
  if out=$("$SHELLCHECK" -s sh "$SCRIPT" 2>&1); then
    ok "shellcheck" "sem aviso"
  else
    fail "shellcheck: $out"
  fi
else
  printf 'skip shellcheck: não instalado neste host\n'
fi

# ── 3. O UID NÃO PODE ESTAR ESCRITO À MÃO ──────────────────────────────────
# Este é o teste que importa. Qualquer número literal numa regra de dono é um
# uid fixo, e uid fixo é o defeito que passou batido. As exceções são a
# família 0/6 (ip6tables é família 6, não uid) e o próprio 127.0.0.1 do
# DoH, que é endereço, não uid.
if grep -nE -- '--uid-owner[[:space:]]+[0-9]+' "$SCRIPT" >/tmp/tabs-blk-uid.txt 2>/dev/null; then
  fail "uid fixo no script (é o defeito que dejó o filtro no ar):
$(cat /tmp/tabs-blk-uid.txt)"
else
  ok "uid" "nenhum uid escrito à mão: o uid vem do package manager"
fi

# e o uid tem que ser lido de verdade, não inventado
if grep -q 'cmd package list packages -U' "$SCRIPT" &&
   grep -q 'dumpsys package' "$SCRIPT" &&
   grep -q 'app_uid' "$SCRIPT"; then
  ok "descoberta do uid" "lê o package manager, com dumpsys de reserva"
else
  fail "o script precisa descobrir o uid em tempo de execução"
fi

# ── 4. AS DUAS FAMÍLIAS, E O reject-TYPE DE CADA UMA ──────────────────────
# Medido: a empresa resolve por IPv6 (2606:4700::6812:1d5f), e um filtro
# só em v4 não filtra o tráfego que importa. E a v6 não aceita o reject-type
# da v4: a regra é recusada na instalação e some em silêncio.
grep -q 'ip6tables' "$SCRIPT" || fail "sem ip6tables: o tráfego IPv6 passa"
grep -q 'icmp6-port-unreachable' "$SCRIPT" || fail "falta o reject-type da IPv6 (icmp6-port-unreachable)"
grep -q 'icmp-port-unreachable' "$SCRIPT" || fail "falta o reject-type da IPv4"
if grep -q 'ip6tables' "$SCRIPT" && grep -q 'icmp6-port-unreachable' "$SCRIPT" &&
   grep -q 'icmp-port-unreachable' "$SCRIPT"; then
  ok "familias" "v4 e v6, cada uma com o reject-type dela"
fi

# ── 5. A LISTA DE DOMÍNIOS ─────────────────────────────────────────────────
# O que a medição viu de verdade tem que estar coberto, senão o filtro é
# decorativo. Estes quatro apareceram no pcap do jogo.
for d in ".xd.com" ".tapapis.com" ".xdgtw.com" ".xindong.com"; do
  if grep -q -- "^$d$" "$SCRIPT"; then
    ok "dominio" "$d coberto"
  else
    fail "$d não está na lista (apareceu no tráfego medido)"
  fi
done

# ── 6. MODO OFFLINE NÃO DEPENDE DE NOME NEM DE IP ──────────────────────────
# É a diferença entre "zero por construção" e "zero enquanto a lista estiver
# certa". No modo offline a regra tem de ser por uid, sem string match.
if grep -q '! -o lo -j REJECT' "$SCRIPT"; then
  ok "modo offline" "rejeita tudo do app menos loopback (não depende de hostname nem de IP)"
else
  fail "o modo offline deveria rejeitar tudo do uid, sem depender da lista de nomes"
fi

# ── 7. O REVERT EXISTE E É DOCUMENTADO ─────────────────────────────────────
# shellcheck disable=SC2016  # as crases aqui sao literais do cabecalho do script, nao-expansao de proposito
if grep -qE 'offline\|sni\|off\)' "$SCRIPT" && grep -q 'REVERT: `sh tabs_offline_block.sh off`' "$SCRIPT"; then
  ok "revert" "modo off no case e documentado no cabecalho"
else
  fail "sem caminho de reversão explicito (modo off) — um filtro sem volta e armadilha"
fi

# ── 8. PERSISTÊNCIA: iptables não sobrevive a reboot ───────────────────────
grep -q 'service.d' "$SCRIPT" || fail "sem hook de boot: no reboot o filtro some e ninguém percebe"
grep -q 'persist' "$SCRIPT" || fail "sem comando persist"
if grep -q 'service.d' "$SCRIPT" && grep -q 'persist' "$SCRIPT"; then
  ok "boot" "hook em service.d para reidratar no reboot"
fi

# ── 9. COMPORTAMENTO COM IPTABLES DE MENTIRA ───────────────────────────────
# Aplica o script contra um iptables falso que registra o que foi pedido, para
# provar que: (a) as duas famílias recebem regra, (b) a ordem dos rejects é a
# de cada família, (c) aplicar duas vezes não duplica regra, (d) `off` limpa.
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/iptables" <<'FAKE'
#!/usr/bin/env bash
# iptables de mentira: registra o pedido, responde o suficiente pro script
# rodar e -- importante -- DELETA de verdade a regra do estado, senão um laço
# de remoção que consulta o estado nunca termina.
echo "$*" >>"$FAKE_LOG"
case "$*" in
  *"-S OUTPUT"*) grep "^-A OUTPUT" "$FAKE_STATE" 2>/dev/null; exit 0 ;;
  *"-C "*)       grep -q -- "$*" "$FAKE_STATE" 2>/dev/null; exit $? ;;
  *"-D OUTPUT "*)
     pos="${3#-A}"; pos=$((pos + 1))   # -S imprime -P na linha 1
     sed -i "${pos}d" "$FAKE_STATE"
     exit 0 ;;
  # -I insere: no estado vira a primeira linha; -A insere no fim
  *"-I OUTPUT "*)
     echo "-A OUTPUT $(echo "$*" | sed 's/^-I OUTPUT [0-9]* //')" >"$FAKE_STATE.new"
     cat "$FAKE_STATE" >>"$FAKE_STATE.new"
     mv "$FAKE_STATE.new" "$FAKE_STATE"
     exit 0 ;;
  *"-A OUTPUT "*) echo "-A OUTPUT $(echo "$*" | sed 's/^-A OUTPUT //')" >>"$FAKE_STATE"; exit 0 ;;
esac
exit 0
FAKE
cp "$TMP/iptables" "$TMP/ip6tables"
chmod +x "$TMP/iptables" "$TMP/ip6tables"
export FAKE_LOG="$TMP/log" FAKE_STATE="$TMP/state"; : >"$FAKE_LOG"; : >"$FAKE_STATE"
# estado prévio: uma regra velha COM comment, que é o caso que o -C sem comment
# não pega e que deixou a regra antiga viva na sessão passada.
printf -- '-A OUTPUT -m owner --uid-owner 10361 -m comment --comment "tabs:sni:jump" -j TABS_SNI\n' >"$FAKE_STATE"
mkdir -p "$TMP/fakebin"
cat >"$TMP/fakebin/cmd" <<'CMD'
#!/usr/bin/env bash
[ "$1" = package ] && echo "package:com.xd.tabs.google uid:10361"
CMD
chmod +x "$TMP/fakebin/cmd"
mkdir -p "$TMP/emptyroot"   # id -u precisa dar root
# `id -u` e o mkdir em /data não podem vir do host: o script precisa de root e
# do caminho do Magisk, e o destino de ambos é sobrescrevivel por env (foi
# assim que o teste ficou possível de rodar sem aparelho).
cat >"$TMP/fakebin/id" <<'ID'
#!/usr/bin/env bash
echo 0
ID
chmod +x "$TMP/fakebin/id"
run_fake() {
  env PATH="$TMP/fakebin:$TMP:$PATH" FAKE_LOG="$FAKE_LOG" FAKE_STATE="$FAKE_STATE" \
      TABS_BLOCK_SERVICE_D="$TMP/service.d" TABS_BLOCK_MODE_FILE="$TMP/mode" \
      sh "$SCRIPT" "$1"
}

# duas aplicações seguidas: o log tem que mostrar tentativa de remover a regra
# antiga com comment (via -D), e o estado final não pode ter duplicata
run_fake offline >/dev/null 2>&1
if grep -q -- "--uid-owner 10361 ! -o lo -j REJECT --reject-with icmp-port-unreachable" "$FAKE_LOG"; then
  ok "aplicar" "regra de uid na v4 com reject-type da v4"
else
  fail "a regra offline da v4 não foi pedida"
fi
if grep -q -- "--uid-owner 10361 ! -o lo -j REJECT --reject-with icmp6-port-unreachable" "$FAKE_LOG"; then
  ok "aplicar v6" "regra de uid na v6 com reject-type da v6"
else
  fail "a regra offline da v6 não foi pedida (ou com o reject-type errado)"
fi
if grep -q -- "^-D OUTPUT" "$FAKE_LOG"; then
  ok "idempotencia" "tenta remover a regra anterior em vez de acumular"
else
  fail "nenhuma remoção tentada: reaplicar acumularia regra"
fi
# segunda aplicação no modo offline tem que derrubar a regra da primeira
: >"$FAKE_LOG"
run_fake offline >/dev/null 2>&1
if grep -q -- "^-D OUTPUT" "$FAKE_LOG" && grep -q -- "^-I OUTPUT 1 -m owner --uid-owner 10361 ! -o lo -j REJECT" "$FAKE_LOG"; then
  ok "idempotencia offline" "a segunda aplicação remove a regra da primeira (alvo REJECT, nao a chain)"
else
  fail "aplicar offline duas vezes acumula regra: a segunda não removeu a da primeira"
fi
# e o estado final tem que ter exatamente uma regra do uid
n_uid=$(grep -c -- "--uid-owner 10361" "$FAKE_STATE" 2>/dev/null || echo 0)
if [ "$n_uid" = 1 ]; then
  ok "uma regra por uid" "so 1 regra do uid 10361 no OUTPUT"
else
  fail "esperava 1 regra do uid no OUTPUT, achei $n_uid"
fi

run_fake off >/dev/null 2>&1
if grep -q "^-F TABS_SNI" "$FAKE_LOG" && grep -q "^-X TABS_SNI" "$FAKE_LOG"; then
  ok "off" "limpa e apaga a chain"
else
  fail "o modo off não limpou a chain"
fi

# ── 10. MODO INVÁLIDO É RECUSADO ───────────────────────────────────────────
if run_fake modo-que-nao-existe >/dev/null 2>&1; then
  fail "aceitou um modo inválido"
else
  ok "modo invalido" "recusado com erro"
fi

if [ "$FAIL" = 0 ]; then
  echo "tabs_offline_block_test: OK"
else
  echo "tabs_offline_block_test: FALHOU" >&2
fi
exit "$FAIL"
