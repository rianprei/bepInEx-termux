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
#
# DUAS CLASSES DE CHECK, ROTULADAS (achado do kilo no a26c025: rótulo que
# promete execução quando o check é presença de texto é twin):
#   texto:    check ESTRUTURAL — greps no FONTE do script. Servem para o
#             contrato (o que o script declara), NÃO para provar efeito.
#             Cada um diz POR QUE texto basta ali (ou o que é só estrutural).
#   execução: a prova real — o script roda contra o iptables/ip6tables de
#             mentira e o ESTADO POR FAMÍLIA é conferido.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/tools/tabs_offline_block.sh"
LOCK="$ROOT/tools/tabs_apk.lock"
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

# ── 3. texto: O UID NÃO PODE ESTAR ESCRITO À MÃO ────────────────────────────
# POR QUE TEXTO BASTA AQUI: o defeito real era literalmente TEXTO no fonte —
# um uid fixo numa regra de dono. O objeto do defeito é o texto, então o
# grep no fonte é a prova, não a promessa. (A execução de que o uid vem do
# package manager é a seção 9, com o `cmd` falso.)
if grep -nE -- '--uid-owner[[:space:]]+[0-9]+' "$SCRIPT" >/tmp/tabs-blk-uid.txt 2>/dev/null; then
  fail "uid fixo no script (é o defeito que dejó o filtro no ar):
$(cat /tmp/tabs-blk-uid.txt)"
else
  ok "texto:uid" "nenhum uid escrito à mão — o defeito era literalmente texto no fonte"
fi

# texto ESTRUTURAL: as chamadas têm de existir para a descoberta existir.
# A prova de efeito está na seção 9 (o fake `cmd` entrega o uid 10361 e a
# regra instalada o usa).
if grep -q 'cmd package list packages -U' "$SCRIPT" &&
   grep -q 'dumpsys package' "$SCRIPT" &&
   grep -q 'app_uid' "$SCRIPT"; then
  ok "texto:descoberta-do-uid" "cmd/dumpsys presentes (efeito: seção 9)"
else
  fail "o script precisa descobrir o uid em tempo de execução"
fi

# ── 4. texto: AS DUAS FAMÍLIAS E OS REJECT-TYPES ────────────────────────────
# POR QUE É SÓ TEXTO (kilo): os 3 greps abaixo eram rotulados "ok: familias —
# v4 e v6, cada uma com o reject-type dela", como se provassem o efeito. Não
# provam: são presença de texto. A PROVA REAL DA EXECUÇÃO é a seção 9, que
# confere o reject-type de CADA família no ESTADO de CADA família.
grep -q 'ip6tables' "$SCRIPT" || fail "sem ip6tables: o tráfego IPv6 passa"
grep -q 'icmp6-port-unreachable' "$SCRIPT" || fail "falta o reject-type da IPv6 (icmp6-port-unreachable)"
grep -q 'icmp-port-unreachable' "$SCRIPT" || fail "falta o reject-type da IPv4"
if grep -q 'ip6tables' "$SCRIPT" && grep -q 'icmp6-port-unreachable' "$SCRIPT" &&
   grep -q 'icmp-port-unreachable' "$SCRIPT"; then
  ok "texto:familias" "ip6tables e os dois reject-types presentes no fonte (efeito: seção 9)"
fi

# ── 5. texto: A LISTA DE DOMÍNIOS ───────────────────────────────────────────
# POR QUE TEXTO BASTA AQUI: os sufixos são o CONTEÚDO DECLARADO do filtro — o
# iptables faz string-match nos LITERAIS da lista (modo sni); o que existe a
# mais ou a menos na lista é revisão de conteúdo, não comportamento
# executável. A prova de que o mecanismo instala é a seção 9 (a chain com as
# regras -A da lista, no modo sni).
for d in ".xd.com" ".tapapis.com" ".xdgtw.com" ".xindong.com"; do
  if grep -q -- "^$d$" "$SCRIPT"; then
    ok "texto:dominio" "$d na lista declarada (apareceu no tráfego medido)"
  else
    fail "$d não está na lista (apareceu no tráfego medido)"
  fi
done

# ── 6. texto: MODO OFFLINE NÃO DEPENDE DE NOME NEM DE IP ────────────────────
# O rótulo antigo ("rejeita tudo do app menos loopback") prometia efeito.
# O EFEITO é a seção 9: a regra executada com ! -o lo no estado por família.
if grep -q '! -o lo -j REJECT' "$SCRIPT"; then
  ok "texto:modo-offline" "regra por uid sem hostname/IP presente no fonte (efeito: seção 9)"
else
  fail "o modo offline deveria rejeitar tudo do uid, sem depender da lista de nomes"
fi

# ── 7. texto: O REVERT EXISTE E É DOCUMENTADO ───────────────────────────────
# Documentação é objeto textual por natureza — mas o EFEITO do off (estados
# limpos, chain apagada) também tem execução na seção 9.
# shellcheck disable=SC2016  # as crases aqui sao literais do cabecalho do script, nao-expansao de proposito
if grep -qE 'offline\|sni\|off\)' "$SCRIPT" && grep -q 'REVERT: `sh tabs_offline_block.sh off`' "$SCRIPT"; then
  ok "texto:revert" "modo off no case e documentado no cabecalho (efeito: seção 9)"
else
  fail "sem caminho de reversão explicito (modo off) — um filtro sem volta e armadilha"
fi

# ── 8. texto: PERSISTÊNCIA ──────────────────────────────────────────────────
grep -q 'service.d' "$SCRIPT" || fail "sem hook de boot: no reboot o filtro some e ninguém percebe"
grep -q 'persist' "$SCRIPT" || fail "sem comando persist"
if grep -q 'service.d' "$SCRIPT" && grep -q 'persist' "$SCRIPT"; then
  ok "texto:boot" "hook em service.d para reidratar no reboot"
fi

# ── 9. O LOCK TRAVA O APK ENTREGUE ──────────────────────────────────────────
# Nada segurava o delivered-sha256: um lock editado (hash errado ou lixo)
# descreveria um APK que não é o instalado, e o patcher "reproduzível"
# reproduziria qualquer coisa. Trava = formato de 64 hex E o valor medido.
lock_sha=$(sed -n 's/^delivered-sha256=//p' "$LOCK" 2>/dev/null)
if [ -n "$lock_sha" ] && printf '%s' "$lock_sha" | grep -qE '^[0-9a-f]{64}$'; then
  ok "lock:formato" "delivered-sha256 é um sha256 (64 hex)"
else
  fail "delivered-sha256 do lock não é um sha256 (64 hex): '$lock_sha'"
fi
if [ "$lock_sha" = "f269964964435e44619dd49a6ad42812360b51cb8e4d11c5160bd2dd77120380" ]; then
  ok "lock:valor" "hash do APK entregue (v5-offline-signed) trava o lock"
else
  fail "delivered-sha256 do lock diverge do APK entregue e medido"
fi

# ── 10. EXECUÇÃO: COMPORTAMENTO COM IPTABLES/IP6TABLES DE MENTIRA ───────────
# Aplica o script contra binários falsos FIÉIS ao real na parte que o script
# depende, e confere o ESTADO POR FAMÍLIA. O que prova, na ordem:
#   (a) as duas famílias recebem regra com o reject-type de cada uma;
#   (b) a regra VELHA (com comment) é removida pelas duas famílias;
#   (c) aplicar duas vezes não duplica regra — POR FAMÍLIA (a v6 agora
#       instala de verdade desde o fix do reject-type, e era a
#       idempotência que o estado compartilhado não exercitava);
#   (d) `off` limpa o estado das duas famílias e apaga a chain.
# INVARIANTE DO SCRIPT, que o teste prende: no máximo UMA regra de filtro
# por (uid, família) no OUTPUT.
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/iptables" <<'FAKE'
#!/usr/bin/env bash
# iptables de mentira, FIEL ao real no que o script usa:
#   - `-S OUTPUT` imprime a POLICY ("-P OUTPUT ACCEPT") primeiro, como o
#     iptables de verdade (é por isso que o drop_uid_rules numera a lista
#     JÁ FILTRADA: o -D recebe número de REGRA, não de linha da saída);
#   - `-D OUTPUT <n>` apaga a REGRA n do estado;
#   - `-C` casa com a regra como se tivesse sido inserida com -A.
# O ESTADO é POR BINÁRIO (FAKE_STATE_DIR/<nome>): o estado compartilhado
# contava a regra da v4 como se fosse da v6 — as famílias têm mundos
# separados no kernel, e o teste passou a imitar isso.
name=$(basename "$0")
STATE="$FAKE_STATE_DIR/$name.state"
echo "$*" >>"$FAKE_LOG"
case "$*" in
  *"-S OUTPUT"*)
     echo "-P OUTPUT ACCEPT"
     grep '^-A OUTPUT' "$STATE" 2>/dev/null
     exit 0 ;;
  *"-C "*)
     req="$*"; req="${req/#-C/-A}"
     grep -q -- "$req" "$STATE" 2>/dev/null
     exit $? ;;
  *"-D OUTPUT "*)
     pos="$3"
     sed -i "${pos}d" "$STATE" 2>/dev/null
     exit 0 ;;
  # -I insere: no estado vira a primeira linha; -A insere no fim
  *"-I OUTPUT "*)
     echo "-A OUTPUT $(echo "$*" | sed 's/^-I OUTPUT [0-9]* //')" >"$STATE.new"
     cat "$STATE" >>"$STATE.new" 2>/dev/null
     mv "$STATE.new" "$STATE"
     exit 0 ;;
  *"-A OUTPUT "*) echo "-A OUTPUT $(echo "$*" | sed 's/^-A OUTPUT //')" >>"$STATE"; exit 0 ;;
esac
exit 0
FAKE
cp "$TMP/iptables" "$TMP/ip6tables"
chmod +x "$TMP/iptables" "$TMP/ip6tables"
export FAKE_LOG="$TMP/log" FAKE_STATE_DIR="$TMP"
: >"$FAKE_LOG"
# estado prévio POR FAMÍLIA: uma regra velha COM comment, que é o caso que
# o -C sem comment não pega e que deixou a regra antiga viva na sessão
# passada — nas DUAS famílias, porque o drop_roda nas duas.
for st in iptables ip6tables; do
  printf -- '-A OUTPUT -m owner --uid-owner 10361 -m comment --comment "tabs:sni:jump" -j TABS_SNI\n' \
      >"$TMP/$st.state"
done
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
  env PATH="$TMP/fakebin:$TMP:$PATH" FAKE_LOG="$FAKE_LOG" FAKE_STATE_DIR="$FAKE_STATE_DIR" \
      TABS_BLOCK_SERVICE_D="$TMP/service.d" TABS_BLOCK_MODE_FILE="$TMP/mode" \
      sh "$SCRIPT" "$1"
}
uid_rule_v4="-m owner --uid-owner 10361 ! -o lo -j REJECT --reject-with icmp-port-unreachable"
uid_rule_v6="-m owner --uid-owner 10361 ! -o lo -j REJECT --reject-with icmp6-port-unreachable"

# primeira aplicação: a regra certa em cada família, e a velha removida
run_fake offline >/dev/null 2>&1
if grep -q -- "--uid-owner 10361 ! -o lo -j REJECT --reject-with icmp-port-unreachable" "$FAKE_LOG"; then
  ok "exec:aplicar-v4" "regra de uid pedida na v4 com reject-type da v4"
else
  fail "a regra offline da v4 não foi pedida"
fi
if grep -q -- "--uid-owner 10361 ! -o lo -j REJECT --reject-with icmp6-port-unreachable" "$FAKE_LOG"; then
  ok "exec:aplicar-v6" "regra de uid pedida na v6 com reject-type da v6"
else
  fail "a regra offline da v6 não foi pedida (ou com o reject-type errado)"
fi
if grep -q -- "^-D OUTPUT" "$FAKE_LOG"; then
  ok "exec:drop-antiga" "tentou remover a regra anterior em vez de acumular"
else
  fail "nenhuma remoção tentada: reaplicar acumularia regra"
fi
# o estado de CADA família: a regra nova (com o reject-type da família) e
# a VELHA (com comment) FORA — o drop por posição funcionou.
if grep -q -- "$uid_rule_v4" "$TMP/iptables.state" && ! grep -q 'tabs:sni:jump' "$TMP/iptables.state"; then
  ok "exec:estado-v4" "v4: regra do uid com reject v4, e a velha com comment removida"
else
  fail "v4: estado errado após aplicar (regra nova ausente ou velha ainda viva)"
fi
if grep -q -- "$uid_rule_v6" "$TMP/ip6tables.state" && ! grep -q 'tabs:sni:jump' "$TMP/ip6tables.state"; then
  ok "exec:estado-v6" "v6: regra do uid com reject v6, e a velha com comment removida"
else
  fail "v6: estado errado após aplicar (regra nova ausente ou velha ainda viva)"
fi

# segunda aplicação: idempotência POR FAMÍLIA — a segunda remove a primeira
: >"$FAKE_LOG"
run_fake offline >/dev/null 2>&1
if grep -q -- "^-D OUTPUT" "$FAKE_LOG" && grep -q -- "^-I OUTPUT 1 -m owner --uid-owner 10361 ! -o lo -j REJECT" "$FAKE_LOG"; then
  ok "exec:idempotencia" "a segunda aplicação remove a regra da primeira (alvo REJECT, nao a chain)"
else
  fail "aplicar offline duas vezes acumula regra: a segunda não removeu a da primeira"
fi
n_v4=$(grep -c -- "--uid-owner 10361" "$TMP/iptables.state" 2>/dev/null) || n_v4=0
n_v6=$(grep -c -- "--uid-owner 10361" "$TMP/ip6tables.state" 2>/dev/null) || n_v6=0
if [ "$n_v4" = 1 ] && [ "$n_v6" = 1 ]; then
  ok "exec:uma-regra-por-familia" "exatamente 1 regra do uid em CADA família (v4=$n_v4 v6=$n_v6)"
else
  fail "esperava 1 regra do uid POR FAMÍLIA, achei v4=$n_v4 v6=$n_v6"
fi

run_fake off >/dev/null 2>&1
if grep -q "^-F TABS_SNI" "$FAKE_LOG" && grep -q "^-X TABS_SNI" "$FAKE_LOG"; then
  ok "exec:off-chain" "limpa e apaga a chain"
else
  fail "o modo off não limpou a chain"
fi
# e o ESTADO das duas famílias sem nenhuma regra do uid: o off de verdade
# deixa o OUTPUT limpo, não só a chain apagada.
if ! grep -q -- "--uid-owner 10361" "$TMP/iptables.state" && ! grep -q -- "--uid-owner 10361" "$TMP/ip6tables.state"; then
  ok "exec:off-estado" "OUTPUT das duas famílias sem nenhuma regra do uid"
else
  fail "o modo off deixou regra do uid no OUTPUT de alguma família"
fi

# ── 11. EXECUÇÃO: modo sni instala a chain com os domínios da lista ────────
# A seção 5 é texto; aqui a lista vira regra -A de verdade na chain.
: >"$FAKE_LOG"
for st in iptables ip6tables; do : >"$TMP/$st.state"; done
run_fake sni >/dev/null 2>&1
if grep -q -- "-j TABS_SNI" "$TMP/iptables.state" && grep -q -- "-A TABS_SNI.*\.xd\.com" "$FAKE_LOG"; then
  ok "exec:sni-v4" "chain TABS_SNI com salto por uid e regra .xd.com instalada"
else
  fail "o modo sni não instalou o salto/regra na v4"
fi
if grep -q -- "-j TABS_SNI6" "$TMP/ip6tables.state" && grep -q -- "-A TABS_SNI6.*\.xd\.com" "$FAKE_LOG"; then
  ok "exec:sni-v6" "chain TABS_SNI6 com salto por uid e regra .xd.com instalada"
else
  fail "o modo sni não instalou o salto/regra na v6"
fi

# ── 12. MODO INVÁLIDO É RECUSADO ────────────────────────────────────────────
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
