#!/system/bin/sh
# tools/tabs_offline_block.sh — corta o tráfego do TABS para os servidores da
# empresa, no próprio aparelho. Reversível, escopado no app, sem mexer em
# /etc/hosts e sem tocar em nenhum outro app.
#
# ───────────────────────────────────────────────────────────────────────────
# MEDIDO NO APARELHO (2026-09-29, uid 10361, v5 f2699649…), o que motivou este
# script e o que ele NÃO consegue fazer:
#
#   · A empresa resolve por IPv6. xdsdk-os-prod-gateway.xd.com -> 2606:4700::
#     6812:1d5f (Cloudflare). Um filtro só em iptables (v4) NÃO filtra nada do
#     que importa: é por isso que este script trata as duas famílias, com o
#     reject-type certo em cada uma (icmp-port-unreachable na v4,
#     icmp6-port-unreachable na v6 — a v6 não aceita o tipo da v4 e a regra
#     some com erro).
#   · Filtrar por SNI é frágil: o match só pega o primeiro pacote do
#     handshake, e o app abre conexões que passam inteiras. Por isso o modo
#     padrão aqui é `offline` (nada sai do app), e o modo `sni` é o
#    detalhe fino, e não o padrão.
#   · O jogo NÃO BOOTA sem a rede da empresa. Medido: com o filtro ativo o app
#     dá "Erro de conexão!" e fica em laço de "VersionEntry Failed: error_code:0"
#     a cada ~3s, para sempre; com o filtro removido o mesmo APK boota limpo
#     (0 retries) e para na tela de login do TapTap. Ou seja: "zero conexão com
#     a empresa" e "jogar" são mutuamente exclusivos neste app. Isso não é
#     opinion: é o resultado dos dois lados, medido no mesmo APK.
#
# POR QUE NÃO /etc/hosts. A lista tem parte estática (26 domínios no dex) e
# parte montada em runtime, com o client_id no nome
# (u0m5lrcp.billboard.ap-sg.tapapis.com) ou com ambiente no host
# (tabs-eur-qa-php.xdgtw.com). hosts não tem wildcard, não casa subdomínio, e
# neste aparelho o /system/etc/hosts é do AdAway, que sobrescreve. Aqui o match
# é no SNI (nome do host, não IP, então CDN girando não escapa) e a posse da
# conexão é por uid, então nenhum outro app é afetado.
#
# POR QUE REJECT E NÃO DROP. O jogo tem timeout longo; com DROP ele fica
# minutos esperando. Com REJECT o erro chega na hora e o app segue o caminho de
# erro dele — que é o que a medição usa.
#
# ESTE SCRIPT NÃO FAZ: não toca em IAP, não toca em licença, não falsifica
# sessão, não fala com servidor nenhum. Ele só impede que os bytes saiam.
#
# USO (no aparelho, como root):
#   sh tabs_offline_block.sh offline|sni|off [pacote]   # padrão: com.xd.tabs.google
#   sh tabs_offline_block.sh status [pacote]
#   sh tabs_offline_block.sh persist on|off
#
#   offline  (privacidade) nada do app sai, exceto loopback. Zero conexão com a
#            empresa por construção, mas o app NÃO BOOTA (ver MEDIDO acima).
#   sni      só as CompanyDomainList/ThirdPartyList por SNI + DoH. Mais fino,
#            e o app também não boota, porque .xd.com é de onde vem o
#            bootstrap.
#   off      sem filtro. O app boota e volta a falar com a empresa.
#
# REVERT: `sh tabs_offline_block.sh off` (imediato) e `persist off` + reboot.
set -eu

PKG="${2:-com.xd.tabs.google}"
CHAIN=TABS_SNI
CHAIN6=TABS_SNI6
# Destino do hook de boot e do arquivo de modo. Overridable para o teste rodar
# sem root e sem /data: num aparelho de verdade é o caminho do Magisk.
SERVICE_D="${TABS_BLOCK_SERVICE_D:-/data/adb/service.d}"
MODE_FILE="${TABS_BLOCK_MODE_FILE:-/data/local/tmp/tabs-offline-mode}"

# Sufixos da empresa. O ponto inicial (".xd.com") casa subdomínio e não casa
# "algumxd.com". Lista = 26 domínios extraídos do dex do APK + os observados
# na medição (incluindo o prefixo do client_id, que é dinâmico).
CompanyDomainList="
.xd.com
.xd.cn
.xdgtw.com
.xindong.com
.xdcdn.net
.tapapis.com
.tapd.io
.taptap.io
.taptapd.com
.tapsvc.io
.tapdb.net
.log-global.aliyuncs.com
"

# Rastreamento de terceiros que apareceu no mesmo fluxo do jogo.
ThirdPartyList="
.googlesyndication.com
.doubleclick.net
.app-measurement.com
.pv.sohu.com
.sentry.io
.appsflyer.com
.adjust.com
"

# Resolvedores que fariam o app ignorar o DNS do sistema e o filtro inteiro.
DohAddrs="8.8.8.8 8.8.4.4 1.1.1.1 1.0.0.1"
DohPorts="53 80 443 853 784 8853"

die() { echo "tabs_offline_block: $*" >&2; exit 1; }

need_root() {
  [ "$(id -u)" = 0 ] || die "precisa de root (su -c 'sh $0 $1')"
  command -v iptables >/dev/null 2>&1 || die "iptables não encontrado"
}

# O uid é lido do package manager na HORA. A versão anterior tinha o uid
# fixo no script: o app foi reinstallado, virou 10361, e a regra continuou
# casando com 10310 — que não era app nenhum. O contador ficava em zero e
# parecia "telemetria desligada", quando na verdade não estava filtrando
# nada. Se este script repetir o erro, o block é teatro.
app_uid() {
  uid="$(cmd package list packages -U "$PKG" 2>/dev/null | sed -n 's/.*uid:\([0-9]*\).*/\1/p' | head -1)"
  [ -n "${uid:-}" ] || uid="$(dumpsys package "$PKG" 2>/dev/null | sed -n 's/.*appId=\([0-9]*\).*/\1/p' | head -1)"
  [ -n "${uid:-}" ] || die "não achei o uid de $PKG (o app está instalado?)"
  echo "$uid"
}

sni_string() { printf '|%s|' "$1"; }

# Apaga QUALQUER salto para a chain que pertença a este uid, inclusive o que
# foi criado com -m comment. `iptables -C` sem o comment não casa com a regra
# que tem comment (é como a regra antiga sobreviveu a uma remoção), então a
# remoção é feita por posição, lida de `iptables -S`.
# Remove QUALQUER regra de OUTPUT que pertença a este uid, de qualquer alvo.
# Invariante do script: no maximo uma regra de filtro por (uid, familia).
# Sem isso, aplicar duas vezes no modo offline -- cujo alvo é REJECT, e não a
# chain -- deixaria a regra antiga viva e a segunda em cima dela.
#
# "--uid-owner" e o alvo NAO precisam ser adjacentes: uma regra criada com
# -m comment tem opções no meio, e era isso que deixava a regra antiga
# sobreviver a uma remoção (o `iptables -C` sem o comment também não casa com
# uma regra que tem comment — foi assim que o filtro da sessão anterior
# sobreviveu a um "remove" e continuou apontando para o uid velho).
drop_uid_rules() {
  _bin="$1"; _uid="$2"
  # Teto de voltas: o laço depende de um comando externo, e um laço sem
  # contador em volta de algo que pode não mudar é como se trava o aparelho.
  _i=0
  while [ "$_i" -lt 64 ]; do
    _i=$((_i + 1))
    n="$($_bin -S OUTPUT 2>/dev/null | grep -n "^-A OUTPUT" \
         | grep -- "--uid-owner $_uid" | head -1 | cut -d: -f1)"
    [ -n "$n" ] || break
    $_bin -D OUTPUT "$((n - 1))" 2>/dev/null || $_bin -D OUTPUT "$n" 2>/dev/null || break
  done
}

build_chain() {
  _bin="$1"; _chain="$2"; _mode="$3"; _reject="$4"
  $_bin -N "$_chain" 2>/dev/null || $_bin -F "$_chain"
  [ "$_mode" = offline ] && return 0
  for d in $CompanyDomainList $ThirdPartyList; do
    $_bin -A "$_chain" -p tcp -m string --algo bm --string "$(sni_string "$d")" \
      -m comment --comment "tabs:sni:$d" -j REJECT --reject-with "$_reject"
  done
  for a in $DohAddrs; do
    for p in $DohPorts; do
      $_bin -A "$_chain" -p tcp -d "$a" --dport "$p" \
        -m comment --comment "tabs:doh:$a:$p" -j REJECT --reject-with "$_reject"
    done
  done
}

apply() {
  need_root
  mode="${1:-offline}"
  case "$mode" in offline|sni|off) ;; *) die "modo inválido: $mode (offline|sni|off)" ;; esac
  uid="$(app_uid)"
  echo "pacote: $PKG  uid: $uid  modo: $mode"

  for fam in 4 6; do
    if [ "$fam" = 4 ]; then bin=iptables; chain=$CHAIN; rej=icmp-port-unreachable
    else bin=ip6tables; chain=$CHAIN6; rej=icmp6-port-unreachable; fi
    drop_uid_rules "$bin" "$uid"
    "$bin" -F "$chain" 2>/dev/null || true
    if [ "$mode" = off ]; then
      "$bin" -X "$chain" 2>/dev/null || true
      continue
    fi
    build_chain "$bin" "$chain" "$mode" "$rej"
    if [ "$mode" = offline ]; then
      # tudo que sai do app, menos loopback. Garantia de zero, sem depender de
      # nome, IP, IPv4/IPv6 ou resolvedor.
      "$bin" -I OUTPUT 1 -m owner --uid-owner "$uid" ! -o lo -j REJECT --reject-with "$rej"
    else
      "$bin" -I OUTPUT 1 -m owner --uid-owner "$uid" -j "$chain"
    fi
  done
  ok=falhou
  for fam in 4 6; do
    if [ "$fam" = 4 ]; then bin=iptables; chain=$CHAIN; else bin=ip6tables; chain=$CHAIN6; fi
    if [ "$mode" = off ]; then
      "$bin" -C OUTPUT -m owner --uid-owner "$uid" -j "$chain" 2>/dev/null && ok="sobrou regra"
      continue
    fi
    if [ "$mode" = offline ]; then
      "$bin" -C OUTPUT -m owner --uid-owner "$uid" ! -o lo -j REJECT --reject-with icmp6-port-unreachable 2>/dev/null ||
      "$bin" -C OUTPUT -m owner --uid-owner "$uid" ! -o lo -j REJECT --reject-with icmp-port-unreachable 2>/dev/null || ok=falhou
    else
      "$bin" -C OUTPUT -m owner --uid-owner "$uid" -j "$chain" 2>/dev/null || ok=falhou
    fi
  done
  echo "verificacao: $ok"
  [ "$mode" = off ] || persist on
}

status() {
  need_root
  uid="$(app_uid)"
  echo "pacote: $PKG  uid: $uid"
  for fam in 4 6; do
    if [ "$fam" = 4 ]; then bin=iptables; chain=$CHAIN; else bin=ip6tables; chain=$CHAIN6; fi
    rules="$($bin -S "$chain" 2>/dev/null | grep -c REJECT || true)"
    pkts="$($bin -L "$chain" -n -v -x 2>/dev/null | awk 'NR>2 && NF>1 {s+=$1} END {print s+0}')"
    if "$bin" -S OUTPUT 2>/dev/null | grep -q -- "--uid-owner $uid "; then
      estado="ativo"
    else
      estado="SEM REGRA para o uid $uid  <-- o filtro nao vale para o app de hoje"
    fi
    echo "  $fam ($bin): $estado, $rules regras, $pkts pacotes rejeitados"
  done
}

off() {
  need_root
  uid="$(app_uid)"
  for fam in 4 6; do
    if [ "$fam" = 4 ]; then bin=iptables; chain=$CHAIN; else bin=ip6tables; chain=$CHAIN6; fi
    drop_uid_rules "$bin" "$uid"
    "$bin" -F "$chain" 2>/dev/null || true
    "$bin" -X "$chain" 2>/dev/null || true
  done
  echo "filtro removido para o uid $uid"
  persist off
}

# Sem isto as regras morrem no reboot e, no dia seguinte, o app está "sem
# filtro" sem ninguém ter mexido em nada — foi o que aconteceu com o
# TABS_SNI/TABS_SNI6 da sessão anterior.
persist() {
  onoff="${1:-on}"
  d="$SERVICE_D"
  f="$d/50-tabs-offline-block.sh"
  case "$onoff" in
    off) rm -f "$f"; echo "boot: nao vai reaplicar" ;;
    on)
      mkdir -p "$d"
      {
        echo "#!/system/bin/sh"
        echo "# gerado por tools/tabs_offline_block.sh"
        echo "sleep 25"
        echo "sh $(dirname "$0")/tabs_offline_block.sh \$(cat $MODE_FILE 2>/dev/null || echo offline) ${PKG} >/dev/null 2>&1"
      } > "$f"
      chmod 0755 "$f"
      echo "boot: $f instalado (modo lido de $MODE_FILE)"
      ;;
    *) die "persist on|off" ;;
  esac
}

case "${1:-status}" in
  offline|sni|off) apply "$1" ;;
  status) status ;;
  persist) persist "${2:-on}" ;;
  *) die "uso: $0 offline|sni|off|status|persist [pacote]" ;;
esac
