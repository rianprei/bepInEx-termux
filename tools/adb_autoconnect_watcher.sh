#!/bin/bash
# Watcher permanente: reconecta o celular via adb wireless automaticamente
# assim que ele aparecer na rede, sem USB, sem comando manual.
# Endurecido (2026-09-15): limpa conexões "offline"/stale ANTES de escanear
# -- achado real: entrada offline (IP antigo, celular trocou de IP via DHCP)
# ficava presa na tabela do adb e confundia checagens seguintes.
# achado de review: subnet hardcoded 192.168.0.x so funciona na rede do
# autor -- override via env pra qualquer outra faixa (ex: 192.168.1).
SUBNET="${ADB_SCAN_SUBNET:-192.168.0}"
while true; do
  # limpa qualquer conexão travada em "offline" antes do scan
  adb devices 2>/dev/null | awk '/offline/{print $1}' | while read -r stale; do
    adb disconnect "$stale" >/dev/null 2>&1
  done

  seq 1 254 | xargs -P100 -I{} bash -c "
    ip=\"${SUBNET}.{}\"
    timeout 0.5 bash -c \"echo >/dev/tcp/\$ip/5555\" 2>/dev/null && echo \"\$ip\"
  " 2>/dev/null | while read -r ip; do
    adb devices 2>/dev/null | grep -q "^$ip:5555.*device$" || adb connect "$ip:5555" >/dev/null 2>&1
  done
  sleep 5
done
