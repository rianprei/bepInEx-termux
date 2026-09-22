#!/bin/bash
# Reconecta ADB wireless no celular sem precisar USB.
# Usa scan de rede (IP muda por DHCP a cada reboot do celular).
# achado de review: subnet hardcoded 192.168.0.x so funciona na rede do
# autor -- override via env pra qualquer outra faixa (ex: 192.168.1).
SUBNET="${ADB_SCAN_SUBNET:-192.168.0}"
echo "Escaneando rede ${SUBNET}.x..."
IP=$(seq 1 254 | xargs -P50 -I{} sh -c "timeout 1 bash -c \"echo >/dev/tcp/${SUBNET}.{}/5555\" 2>/dev/null && echo ${SUBNET}.{}" | head -1)
if [ -z "$IP" ]; then
  echo "Celular não encontrado na rede ${SUBNET}.x com porta 5555 aberta."
  exit 1
fi
echo "Achado: $IP"
adb connect "$IP:5555"
adb -s "$IP:5555" shell getprop ro.product.model
