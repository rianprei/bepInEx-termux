#!/bin/bash
# Reconecta ADB wireless no celular sem precisar USB.
# Usa scan de rede (IP muda por DHCP a cada reboot do celular).
echo "Escaneando rede..."
IP=$(seq 1 254 | xargs -P50 -I{} sh -c 'timeout 1 bash -c "echo >/dev/tcp/192.168.0.{}/5555" 2>/dev/null && echo 192.168.0.{}' | head -1)
if [ -z "$IP" ]; then
  echo "Celular não encontrado na rede 192.168.0.x com porta 5555 aberta."
  exit 1
fi
echo "Achado: $IP"
adb connect "$IP:5555"
adb -s "$IP:5555" shell getprop ro.product.model
